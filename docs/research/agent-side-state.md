# Research: agent state beside the conversation in Codex and Claude Code

Status: research notes, 5 November 2026. Nothing here is decided. Decisions
taken from it go on [the Agent View map](https://github.com/flotilla-org/wheelhouse/issues/686)
and the side Views ticket [#692](https://github.com/flotilla-org/wheelhouse/issues/692).

Question: [#688](https://github.com/flotilla-org/wheelhouse/issues/688). Besides
the conversation, what state do Codex and Claude Code expose (subagents,
background tasks, checklists, plans, and anything else with its own lifecycle),
and where can a client get it? For each kind: how it appears on disk and live,
whether it arrives whole or as changes, what identifies it and links it to the
conversation, and how other apps show it beside the chat.

This extends [`agent-log-classes.md`](https://github.com/flotilla-org/wheelhouse/blob/research/agent-log-classes/docs/research/agent-log-classes.md)
(#666), which classified conversation records, and the
[composer survey](https://github.com/flotilla-org/wheelhouse/blob/research/agent-composers/docs/research/agent-composers.md).
Neither is repeated here.

**Sources.**

| Source | Version | Written below as |
|---|---|---|
| `openai/codex` | `ee80baa38e3919061e0e0ad022aba1d9fbfc5dbc` (2026-11-05); paths under `codex-rs/` | `codex:` |
| Codex rollouts on this host | 511 files under `~/.codex/sessions`, codex-cli up to 0.162.1; shapes and counts only | `rollouts:` |
| Claude Code CLI bundle | `~/.claude/remote/ccd-cli/2.1.317-a5bf08beebc8`, read with byte searches; it embeds the SDK wire schemas as Zod with `.describe()` text. Minified names are not quoted as stable | `cli:` |
| Claude Agent SDK, Python | `claude-agent-sdk` 0.2.178 (bundles CLI 2.1.318), `src/claude_agent_sdk/` | `sdk-py:` |
| Claude Code and Agent SDK docs | <https://code.claude.com/docs/en/> pages as linked, read 2026-11-05 | URL |
| Claude Code transcripts on this host | 96 main and 119 subagent `.jsonl` under `~/.claude/projects`; keys and counts only | `transcripts:` |
| `pingdotgg/t3code` | `337c5858c33bc7f8f6dfd391a2c279fa0f7e4d17` (2026-11-05) | `t3:` |
| `anomalyco/opencode` | `a316ae64c397cff9ad110e6b9277d5dba5002a2e` (2026-11-05) | `opencode:` |
| `zed-industries/zed` | `654efae441bc8eb117d0042bb28cd7cf3570bdc9` (2026-11-05); `agent-client-protocol` 5.0.0, schema 1.15.1 | `zed:` |
| `ag-ui-protocol/ag-ui` | `d540f5934429bd548ba9c53da286028958e7506a` (2026-11-05); `sdks/typescript/packages/core/src/generated/types.ts` | `agui:` |
| Codex app and IDE docs | <https://learn.chatgpt.com/docs/> (developers.openai.com/codex now redirects there), read 2026-11-05 | URL |

No content from real sessions is copied here. All examples are synthetic.
**[unconfirmed]** marks a point not found in a primary source.

## Summary

- **Live interfaces carry much more than the logs.** Codex writes neither
  `PlanUpdate`, approvals, nor terminal output deltas to the rollout
  (`codex: rollout/src/policy.rs:161-227`). Claude Code writes none of its
  `task_*` system messages to the transcript. On disk, side state mostly has to
  be rebuilt from tool calls and their results.
- **Checklists differ in shape.** Codex `update_plan` and Claude `TodoWrite`
  send the whole list each time. Claude's newer `TaskCreate`/`TaskUpdate`
  send changes, so a client has to build the list. In Claude Code 2.1.317
  none of these tools is on by default for current models; the desktop app
  brings its own checklist tool instead.
- **Subagents are child threads in Codex and child transcripts in Claude.**
  Codex gives each child its own thread id, rollout and live event stream.
  Claude gives each its own `agent-<id>.jsonl`, but live it interleaves child
  messages in the parent stream, tagged with `parent_tool_use_id`. Both link
  the child to the parent's spawning tool call.
- **Background terminals are the weakest state.** Codex sends nothing while a
  terminal runs between turns. A client must ask
  `thread/backgroundTerminals/list`. Claude reports shells through the same
  `task_*` messages as agents, and also in `background_tasks_changed`, a
  whole-set snapshot. Neither harness restarts them on resume.
- **Apps share one layout.** Each change stays as a row in the transcript.
  A small "live now" strip above the composer shows the current checklist
  item, running work and pending approvals. Each subagent opens as its own
  thread with a way back. The strip is derived from data the app already has.
  No app keeps a separate document for it, and it hides once the work is
  done.
- **AG-UI covers half of this.** `Subagent*` events plus `subagentRunId` fit
  Claude's model. `ActivitySnapshot`/`Delta` fit per-item progress, but they
  are inline messages, not docked state. `StateSnapshot`/`Delta` is one
  document per run, with no per-kind key. AG-UI has no child thread id, no
  background work that outlives a run, and no way to list state on attach.

## 1. Checklists and plans

### 1.1 Codex

There are two separate mechanisms.

**The checklist: `update_plan`.**

- **Live.** The app-server sends `turn/plan/updated`
  `{threadId, turnId, explanation?, plan: [{step, status: pending|inProgress|completed}]}`
  (`codex: app-server-protocol/src/protocol/v2/turn.rs:630-652`, emitted at
  `app-server/src/bespoke_event_handling.rs:1323-1383`). It is a
  notification, not a `ThreadItem`.
- **Disk.** `EventMsg::PlanUpdate` is not written
  (`codex: rollout/src/policy.rs:212`). Only the `function_call` named
  `update_plan` survives, with arguments `{explanation?, plan: [{step, status}]}`
  (`codex: protocol/src/plan_tool.rs:7-29`). Its output is the fixed string
  "Plan updated" (`codex: core/src/tools/handlers/plan.rs:22`). On this
  host: 2,270 `update_plan` calls in 117 rollouts, and no `plan_update`
  events (`rollouts:`).
- **Whole or changes:** whole. Each call replaces the list.
- **Identity:** only `turnId`. The handler drops the call id
  (`codex: core/src/tools/handlers/plan.rs:73`), so the notification can't be
  tied to an item.
- **Lifecycle:** none. The latest list wins. **It does not survive resume.**
  `thread/read` and `thread/resume` rebuild turns from `ItemCompleted` and a
  few events (`codex: app-server-protocol/src/protocol/thread_history.rs:443-531`).
  Resume re-sends only token usage, the goal and pending requests
  (`codex: app-server/src/request_processors/thread_lifecycle.rs:1260-1285`).
  A client that wants the checklist after a restart has to parse the
  `update_plan` arguments from the rollout itself.

**The plan-mode proposal: the `Plan` item.**

- **Live:** `item/started`, then `item/plan/delta {threadId, turnId, itemId, delta}`
  (experimental), then `item/completed` with `{type: plan, id, text}`
  (`codex: app-server-protocol/src/protocol/v2/item.rs:336-342, 1561-1566`).
  The completed text is authoritative.
- **Disk:** `item_completed` with the `Plan` item (`codex: rollout/src/policy.rs:244`).
  None on this host; plan mode isn't used here.
- **Identity:** item id `"{turn_id}-plan"` (`codex: core/src/session/turn.rs:2314`).
- **Lifecycle:** started, then completed. It survives replay
  (`codex: app-server-protocol/src/protocol/thread_history.rs:782`).
  `update_plan` is rejected while in plan mode
  (`codex: core/src/tools/handlers/plan.rs:87-91`), so the two never
  overlap within a turn.

Synthetic example:

```json
{"method":"turn/plan/updated","params":{"threadId":"thr_A","turnId":"turn_3",
 "explanation":null,"plan":[{"step":"Read the parser","status":"completed"},
 {"step":"Fix the last-line bug","status":"inProgress"},
 {"step":"Add a test","status":"pending"}]}}
```

### 1.2 Claude Code

**Which tool is on.** In 2.1.317 the checklist tools are given only to
Opus 4.7, Sonnet 4.6, Haiku 4.5 and older models, unless the host opts in with
`CLAUDE_CODE_ENABLE_TODO_TOOLS=1` or by naming a tool in `tools`/`allowedTools`.
Background and cloud sessions get them on every model
(<https://code.claude.com/docs/en/tools-reference#task-tool-availability>;
`cli:` a model allow-list ending at `claude-opus-4-7`). Where they are on, the
`Task*` tools are the default. `TodoWrite` is used only with
`CLAUDE_CODE_ENABLE_TASKS=0` (`cli:`).

| | `TodoWrite` (legacy) | `TaskCreate` / `TaskUpdate` / `TaskGet` / `TaskList` |
|---|---|---|
| Input | `{todos: [{content, status, activeForm}]}`, the whole list | `TaskCreate {subject, description, activeForm?, metadata?}`; `TaskUpdate {taskId, status?: pending\|in_progress\|completed\|deleted, subject?, description?, activeForm?, addBlocks?, addBlockedBy?, owner?, metadata?}` |
| `toolUseResult` on disk | `{oldTodos, newTodos}`, both whole | `TaskCreate {task: {id, subject, description}}`; `TaskUpdate {success, taskId, updatedFields, statusChange?: {from, to}}` |
| Whole or changes | Whole | **Changes.** The id exists only in the create result, so a reader pairs each create call with its result |
| Stored | In process memory per agent [unconfirmed: no `~/.claude/todos` path found in 2.1.317] | `~/.claude/tasks/<listId>/`, one file per task. The list id is `CLAUDE_CODE_TASK_LIST_ID`, else the team name, else the session id (`cli:`) |
| Survives | [unconfirmed] | Compaction and `--resume`; a shared list id shares it across sessions (<https://code.claude.com/docs/en/interactive-mode#task-list>) |

- **Live:** only the tool calls and results. The SDK's todo page says todo
  activity arrives as `tool_use` blocks
  (<https://code.claude.com/docs/en/agent-sdk/todo-tracking>). The raw input in
  the stream may use the model's own key spellings (`task_id`, `active_form`),
  which the CLI normalises only before running the tool (`cli:`).
- **Identity:** the task id from the create result; the tool call id of each
  change. A subagent's `Task*` calls write the same list.
- **Hooks:** `TaskCreated` and `TaskCompleted` exist
  (<https://code.claude.com/docs/en/hooks>); their fields are [unconfirmed].
- **None on this host.** `~/.claude/tasks` and `~/.claude/todos` don't exist
  and the transcripts hold no `TodoWrite` or `Task*` calls. Sessions run by the
  desktop app use the app's own MCP tool, `mcp__ccd_turn__update_plan`
  (385 calls in `transcripts:`). Its input, as the tool schema states, is
  either a whole list (`{items: [{content, status}], scope}`, 272 calls) or a
  step (`{advance: true}` 90, `{done: [2], start: 4}` 23). So a log reader
  sees both shapes in one tool.

**Plan mode.** `ExitPlanMode` input is `{plan, planFilePath}`; the plan is
Markdown kept in `~/.claude/plans/<slug>.md`, and every transcript record
carries the session's `slug` (`cli:`). In agent teams, plan approval travels as
mailbox frames `plan_approval_request` / `plan_approval_response` (`cli:`).

## 2. Subagents

### 2.1 Codex

- **Tools.** Two generations, chosen by `MultiAgentVersion`
  (`codex: protocol/src/protocol.rs:3411`):
  - v1: `spawn_agent`, `send_input`, `resume_agent`, `wait`, `close_agent`;
  - v2: `spawn_agent`, `send_message`, `followup_task`, `wait_agent`,
    `interrupt_agent`, `list_agents`
    (`codex: core/src/tools/handlers/multi_agents_v2/`).
- **Each child is a thread** with its own rollout file. Its `session_meta.source`
  is `{subagent: {thread_spawn: {parent_thread_id, depth, agent_path,
  agent_nickname, agent_role}}}` (`codex: protocol/src/protocol.rs:3236-3251`).
  250 of 516 `session_meta` records on this host are spawned children
  (`rollouts:`).
- **In the parent:**
  - `collabAgentToolCall` items:
    `{id, tool, status, senderThreadId, receiverThreadIds, prompt?, model?,
    agentsStates: {threadId: {status, message}}}`
    (`codex: app-server-protocol/src/protocol/v2/item.rs:437-458`). v1 tools
    and v2 `wait_agent` produce these.
  - `subAgentActivity` items: `{id, kind: started|interacted|interrupted|completed,
    agentThreadId, agentPath, model?}` (`item.rs:460-469`). v2 spawn, send
    and follow-up produce these.
  - Child completion also reaches the parent model as a
    `<subagent_notification>{"agent_path", "status"}` user message
    (`codex: core/src/context/subagent_notification.rs:36-47`). Clients
    should not show it; it isn't a `UserMessage` item.
- **Agent status:** `pendingInit`, `running`, `interrupted`, `completed`,
  `errored`, `shutdown`, `notFound` (`item.rs:1351-1359`). The tool-call item
  itself is `inProgress|completed|failed|interrupted` (`item.rs:1293-1298`).
- **Whole or changes:** changes, one item per tool call. Each item carries a
  snapshot of the states of the agents it touched.
- **Identity and link:** the item id is the tool call id; the child's
  `threadId`; `agentPath` (the parent's path is the child's minus its last
  segment, `codex: core/src/agent/control.rs:507-511`). The child's `Thread`
  carries `parentThreadId`, `agentNickname`, `agentRole`
  (`codex: app-server-protocol/src/protocol/v2/thread_data.rs:218-282`).
- **Listing and watching.** `thread/list` with `parentThreadId` or
  `ancestorThreadId`, passing `sourceKinds` that include `subAgentThreadSpawn`,
  since the default lists interactive threads only
  (`codex: app-server-protocol/src/protocol/v2/thread.rs:1455-1569`). The
  app-server subscribes every attached connection to a new child
  (`codex: app-server/src/lib.rs:1338-1356`), but sends no `thread/started`
  for it. A client learns the child from `receiverThreadIds` or
  `agentThreadId`, then receives the child's `item/*`, `turn/*` and
  `thread/status/changed` keyed by the child's thread id.
- **Survives:** both item kinds replay
  (`codex: app-server-protocol/src/protocol/thread_history.rs:785-786`); child
  threads are read or resumed on their own.

### 2.2 Claude Code

- **Tool.** `Agent` (renamed from `Task` in 2.1.63):
  `{description, prompt, subagent_type?, run_in_background?, isolation?: "worktree", name?, model?, cwd?}`.
  On this host `run_in_background` appears as `true`, `"true"`, `false` or
  absent, so parse it leniently (`transcripts:`).
- **Disk, parent side.** The `toolUseResult` of the call has three shapes:
  - `async_launched`:
    `{status, isAsync, agentId, description, prompt, outputFile, canContinueAgent, ...}`
    (114 on this host);
  - `completed`:
    `{status, agentId, agentType, content, totalDurationMs, totalTokens, totalToolUseCount, ...}`
    (2);
  - `remote_launched`: `{taskId, ...}`.

  Completion of a background agent arrives as a queued user turn with
  `origin.kind: task-notification` and a `task_status` attachment
  `{taskId, taskType: "local_agent", description, status, deltaSummary, outputFilePath}`.
- **Disk, child side.** `<project>/<session>/subagents/agent-<agentId>.jsonl`.
  Every record has `isSidechain: true` and `agentId`. Beside it,
  `agent-<id>.meta.json` holds `{agentType, description, toolUseId, spawnDepth, worktreePath?, ...}`.
  `<tmp>/claude-<uid>/<project>/<session>/tasks/<agentId>.output` is a symlink
  to the same transcript (`transcripts:`, `cli:`).
- **Live.** Child messages are interleaved in the parent stream and carry
  `parent_tool_use_id`. A foreground child's text and thinking are forwarded
  only with the `forwardSubagentText` option. Lifecycle comes as `system`
  messages (`sdk-py: types.py:1539-1646`; `cli:`):
  - `task_started {task_id, tool_use_id?, description, task_type, subagent_type?, is_backgrounded?, spawn_depth?, parent_task_id?, prompt?}`;
  - `task_progress {task_id, tool_use_id?, usage: {total_tokens, tool_uses, duration_ms}, last_tool_name?, summary?}`.
    `summary` needs the `agentProgressSummaries` option;
  - `task_updated {task_id, patch: {status?: pending|running|paused|completed|failed|killed, description?, end_time?, error?, is_backgrounded?}}`,
    a merge patch;
  - `task_notification {task_id, tool_use_id?, status: completed|failed|stopped, output_file, summary, usage?}`.
    The SDK warns that this can be suppressed after a stop, so treat a terminal
    `task_updated` as an end too;
  - `background_tasks_changed {tasks: [{task_id, task_type, subagent_type?, description, parent_task_id?, awaited?}]}`,
    the whole background set on every change (see §3.2 for its limit).
- **Hooks:** `SubagentStart {agent_id, agent_type}`,
  `SubagentStop {agent_id, agent_type, agent_transcript_path, last_assistant_message?, background_tasks?, session_crons?}`
  (`sdk-py: types.py:329-490`).
- **Methods:** live, `stopTask(taskId)`, `backgroundTasks(toolUseId?)` (the
  Ctrl+B action) and `supportedAgents()` (TypeScript docs,
  <https://code.claude.com/docs/en/agent-sdk/typescript>). From files, the
  Python SDK has `list_subagents(session_id)` and
  `get_subagent_messages(session_id, agent_id)`
  (`sdk-py: _internal/sessions.py:1514-1556`); [unconfirmed] in TypeScript.
- **Identity and link:** `agentId` (`a` plus 16 hex digits) equals `task_id`
  for `local_agent` tasks; `tool_use_id` names the spawning `Agent` call, and
  `meta.json` repeats it as `toolUseId`. Other task id prefixes in the bundle:
  `b` shell, `t` teammate, `r` remote agent, `w` workflow, `m`/`s` monitors
  (`cli:`).
- **Survives:** transcripts persist (deleted after `cleanupPeriodDays`, default
  30) and an agent can be continued with `SendMessage {to}`. Running tasks end
  as `stopped` with `reason: worker_restart` when the worker restarts
  (`sdk-py: types.py:1595-1646`).

## 3. Background terminals and other running tasks

### 3.1 Codex: unified exec

- **Live.** `item/started` with a `commandExecution` item (`status: inProgress`,
  `processId`; `codex: app-server-protocol/src/protocol/v2/item.rs:375`), then
  `item/commandExecution/outputDelta` and
  `item/commandExecution/terminalInteraction {itemId, processId, stdin}` for
  each `write_stdin` (`item.rs:1606-1612`). `item/completed` comes only when
  the process exits. A watcher sends it with the **original turn id and call
  id**, which can be long after `turn/completed`
  (`codex: core/src/unified_exec/async_watcher.rs:155-240`).
- **Nothing reports a terminal still running.** Thread status covers only
  loaded, running, waiting on approval or input, and system error
  (`codex: app-server/src/thread_status.rs:432-463`). The only sign is an item
  that started and hasn't completed.
- **Listing and control** (all experimental,
  `codex: app-server-protocol/src/protocol/common.rs:771-788`):
  `thread/backgroundTerminals/list` returns
  `{data: [{itemId, processId, command, cwd, osPid, cpuPercent, rssKb}], nextCursor}`
  (`thread.rs:1260-1291`). `osPid`, `cpuPercent` and `rssKb` are always null
  (`codex: app-server/src/request_processors/thread_processor.rs:2656-2658`).
  `…/terminate {threadId, processId}` and `…/clean` stop one or all. The
  TUI's `/ps` uses `list` (`codex: tui/src/app_server_session.rs:2009-2011`).
- **Disk:** the `exec_command` and `write_stdin` calls, plus one
  `CommandExecution` item when the process exits (output cut to 64 KiB). Start,
  deltas and interactions are not written
  (`codex: rollout/src/policy.rs:194-196, 215`).
- **Identity:** item id = the `exec_command` call id; `processId` is the
  unified-exec session id, not an OS pid; `turnId` is the turn that started it.
- **Survives:** no. Processes die with the session
  (`codex: core/src/session/handlers.rs:65-66`), and only completed items
  replay. Whether a reattaching client sees a still-open item is
  [unconfirmed]: `ItemStarted` isn't persisted.

### 3.2 Claude Code: background shells, monitors and agents

- **One task model for all of them.** Shells (`local_bash`), agents
  (`local_agent`), monitors and workflows share the `task_*` messages and
  `background_tasks_changed` (§2.2).
- **Disk:** `Bash {run_in_background: true}` (123 on this host); its
  `toolUseResult` gains `backgroundTaskId` (`b` plus 8 characters),
  `backgroundCwdHint`, and `backgroundedByUser` when the user pressed Ctrl+B.
  Output goes to `<tmp>/claude-<uid>/<project>/<session>/tasks/<id>.output`.
  `Monitor {command, description, timeout_ms}` returns `{taskId, persistent, timeoutMs}`.
  `TaskStop {task_id}` stops one. Completion is a task-notification turn.
  `BashOutput`, `KillShell` and `TaskOutput` are gone (`TaskOutput` removed in
  2.1.277); the model reads the output file instead (`cli:`).
- **Live control requests** (`cli:`): `get_task_output {task_id}` returns the last
  8 KiB as `{output, total_bytes, truncated}`, which is what the TUI's `/tasks`
  detail reads; `stop_task`; `background_tasks {tool_use_id?}`.
- **`background_tasks_changed` is not a full registry.** It lists only work
  that is backgrounded. A subagent starts in the foreground and may move to the
  background later without a second `task_started`, so the snapshot can omit
  running work. The Python SDK deliberately ignores it and tracks the
  lifecycle frames instead (`sdk-py: _internal/query.py:926-935`). A shell may
  also never reach a terminal status (a dev server).
- **Limits:** unattended sessions (`-p`, SDK, cloud) cap background commands
  at 30 minutes by default, 2 hours at most; interactive sessions have no cap.
  Monitors default to 5 minutes. A foreground subagent's shells die with it
  (`cli:`).
- **Survives:** no. Shells and monitors are not restarted on resume
  (<https://code.claude.com/docs/en/scheduled-tasks#limitations>).

Synthetic example of the live frames for one background shell:

```json
{"type":"system","subtype":"task_started","task_id":"b1f2e3d4c","tool_use_id":"toolu_01X","description":"Run dev server","task_type":"local_bash"}
{"type":"system","subtype":"background_tasks_changed","tasks":[{"task_id":"b1f2e3d4c","task_type":"local_bash","description":"Run dev server"}]}
{"type":"system","subtype":"task_updated","task_id":"b1f2e3d4c","patch":{"status":"killed","end_time":1762358400000}}
```

## 4. Other state with its own lifecycle

| Kind | Codex | Claude Code |
|---|---|---|
| **Pending approval** | Server requests `item/commandExecution/requestApproval`, `item/fileChange/requestApproval`, `item/permissions/requestApproval` (`codex: app-server-protocol/src/protocol/common.rs:1866-1891`), keyed by `threadId, turnId, itemId`; closed by `serverRequest/resolved {threadId, requestId}`. Not written to disk. **Re-sent to a client that resumes a loaded thread** (`codex: app-server/src/outgoing_message.rs:525-560`). Thread status shows `active {activeFlags: [waitingOnApproval]}` | Control request `can_use_tool {tool_name, input, tool_use_id, agent_id?, permission_suggestions?, decision_reason?, title?, ...}`; `session_state_changed {state: idle\|running\|requires_action}`; `PermissionRequest` hook. Not stored as pending; user records may carry `permissionDecision` (`cli:`). A background subagent's prompt surfaces in the main session, naming the subagent (<https://code.claude.com/docs/en/sub-agents>) |
| **Question to the user** | `item/tool/requestUserInput {threadId, turnId, itemId, questions, isBlocking}`; MCP `mcpServer/elicitation/request`. Same replay; flag `waitingOnUserInput` | `AskUserQuestion {questions}` → result `{questions, answers}`; goes through the permission path [unconfirmed which] |
| **Scheduled wakeup** | No cron-style scheduling found [unconfirmed]. A durable input queue: `thread/queue/changed {threadId}` (a signal; then `thread/queue/list`), plus `add/update/delete/reorder` (experimental). A `sleep {id, durationMs}` extension item | `CronCreate {cron, prompt, recurring?, durable?}` → `{id, humanSchedule, nextFireAt?}`; durable ones in `.claude/scheduled_tasks.json`; `ScheduleWakeup {delaySeconds, reason, prompt}`. A fire arrives as a turn with `origin.kind: task-notification`, `subkind: scheduled-trigger`. `SubagentStop`/`Stop` hooks list `session_crons`. On resume, unexpired cron tasks return, self-paced loops don't (`cli:`; the restore path is [unconfirmed]) |
| **Goal** | `thread/goal/updated {threadId, goal: {objective, status: active\|paused\|blocked\|usageLimited\|budgetLimited\|complete, tokensUsed, ...}}`, `thread/goal/cleared` (`thread.rs:821-848`). Written to disk and re-sent on resume | None found |
| **Thread or session status** | `thread/status/changed {status: notLoaded\|idle\|systemError\|active {activeFlags}}` | `session_state_changed`; `~/.claude/sessions/<pid>.json` `{status: idle\|busy, ...}` |
| **Turn diff** | `turn/diff/updated {threadId, turnId, diff}`, the whole diff for the turn; not written | File history on disk (`file-history-*` records, #666) |
| **Usage** | `thread/tokenUsage/updated` (re-sent on resume), `account/rateLimits/updated` | `cost-state` records; `get_context_usage` control request |
| **MCP servers** | `mcpServer/startupStatus/updated {name, status: starting\|ready\|failed\|cancelled}` | `system/init.mcp_servers`; `mcpServerStatus()` |
| **Compaction** | `contextCompaction` item | `compact_boundary` (#666) |

The full list of Codex v2 notifications is in
`codex: app-server-protocol/src/protocol/common.rs:2019-2141`. Besides the
ones above, the side-state ones are `hook/started`/`completed`,
`item/autoApprovalReview/*`, `thread/settings/updated` and `thread/closed`.

**Several watchers on one thread.** Codex supports it: each thread keeps a set
of subscribed connections, `thread/resume` on a running thread replays history
and subscribes in one step, `thread/unsubscribe` leaves, server requests go to
every subscriber, and `serverRequest/resolved` tells the others
(`codex: app-server/src/thread_state.rs:67, 361`;
`app-server/src/outgoing_message.rs:234, 388`). Claude's SDK is one process
per client; a second `initialize` gets a fresh `background_tasks_changed`
snapshot (`cli:`), but there is no shared-watch model.

## 5. How other apps show it

Every app splits the same three ways:

1. **Each change is a row in the transcript**, where it happened.
2. **A "live now" strip above the composer** shows the current checklist
   item, running subagents and shells, and pending approvals. It is computed
   from data the app already has, and hides when the work is done.
3. **Each subagent opens as its own thread**, with a control back to the
   parent. In the child, the composer is often replaced by a status bar.

| App | Checklist | Subagents | Background shells | Approvals | Persists? |
|---|---|---|---|---|---|
| **Claude Code TUI** | Status area above the prompt, Ctrl+T, up to 5 items; expansion restored on `--resume` | Tree panel below the prompt; a finished row goes at once (failed or stopped stay 30 s); `/tasks` lists all | `/tasks`, with `x` to stop | Background subagents' prompts in the main session, naming the subagent | List survives compaction and resume; panel rows don't outlive the work ([interactive mode](https://code.claude.com/docs/en/interactive-mode), [sub-agents](https://code.claude.com/docs/en/sub-agents)) |
| **Claude desktop** | "Plan" pane | "Tasks" pane: subagents, shells, workflows; click opens output or stops | Same pane | [unconfirmed] | Panes appear "once the session has something to show"; persistence [unconfirmed] ([desktop](https://code.claude.com/docs/en/desktop)) |
| **Claude VS Code** | [unconfirmed] | Background subagents as one status line under their Agent card (2.1.304) | In the agent map, each with Stop (2.1.277) | [unconfirmed] | ([CHANGELOG](https://github.com/anthropics/claude-code/blob/main/CHANGELOG.md)) |
| **Codex TUI** | Inline "Updated Plan" cell only (`codex: tui/src/history_cell/plans.rs:235-273`) | `/subagents`, `/agents` command centre, Alt+←/→ between threads (`codex: tui/src/slash_command.rs:144-145`) | Status line "N background terminal running · /ps to view" (`codex: tui/src/status_indicator_widget.rs:503`); `/ps`, `/stop` | Overlay names the source thread; `o` opens it | Transcript only |
| **Codex app / IDE** | [unconfirmed] | IDE: "active subagents appear above the composer", expand to see status, stop all, open a thread; app: read-only Active and Done lists | [unconfirmed] | | Goal progress row above the composer ([subagents](https://learn.chatgpt.com/docs/agent-configuration/subagents), [long-running work](https://learn.chatgpt.com/docs/long-running-work)) |
| **T3 Code** | A quiet inline row "Tasks 2/5 · current" that expands (`t3: apps/web/src/session-logic.ts:517-527`); the right-hand "plan" and "agents" panels were removed (`t3: apps/web/src/rightPanelStore.ts:492`) | "ThreadNow" line above the composer: status, progress, ticking time, Open (child thread), Stop (`t3: apps/web/src/components/chat/ThreadActivityBanners.tsx:156-243`); child's composer replaced by a bar with "open parent" | Same ThreadNow line | Child approvals asked in the parent | Server-side graph in SQLite; survives restart |
| **opencode web** | Pill in the composer lane, popover with the list; shown only "while the turn runs and still has work left" (`opencode: packages/app/src/session/composer/region.tsx:468-472`) | One capsule above the composer listing subagents and shells, with time and Stop; opens the child session (`opencode: .../composer/activity-chips.tsx:17-116`) | Same capsule, with output tail | | List is derived from the last `todowrite` call, so it survives reload and respects revert (`opencode: packages/app/src/session/composer/todos.ts:12-20`) |
| **opencode TUI** | Sidebar "Todo" section while non-empty, plus inline `# Todos` blocks (`opencode: packages/tui/src/feature-plugins/sidebar/todo.tsx:6-56`) | "Subagents" tab in the composer drawer: the session tree with status | "Shell" tab, output and kill | | Sidebar persists after completion |
| **Zed agent panel** | "Activity bar" between the thread and the editor: collapsed "Current: … · N left", expandable, dismissible (`zed: crates/agent_ui/src/conversation_view/thread_view.rs:4848-4990`) | Inline card (status, model, files changed) that expands to the child's slice; Maximize opens the child thread, Minimize returns (`zed: .../subagent_card.rs`, `thread_view.rs:12130-12560`) | None; terminal output stays in the tool card | Top of the activity bar, including "Subagents Awaiting Permission: N"; clicking scrolls to the card | Plan in memory; ACP v1 plans drop completed entries at the next turn (`zed: crates/acp_thread/src/acp_thread.rs:7171-7178`) |

Two data-model points from these apps:

- **T3 Code's graph** separates a `PlanArtifact` (`proposed_plan` or
  `todo_list`, `status: draft|active|completed|superseded`), an
  `OrchestrationV2Subagent` (`parentNodeId`, `childThreadId`, `nativeTaskRef`,
  `progress`) and a `PendingBackgroundTask`
  (`kind: subagent|command|monitor|background_task`, `awaited`)
  (`t3: packages/contracts/src/orchestrationV2.ts:884-1037, 1320-1368`). Its
  rule: "Child/subagent todo lists are nested under their execution node.
  Child plans do not replace root plans"
  (`t3: docs/orchestration-v2/feature-lifecycles.md:480-508`). `awaited`
  separates work the agent will resume on from a server left running.
- **ACP's plan** is a whole list ("The Client MUST replace the current plan
  completely", <https://agentclientprotocol.com/protocol/agent-plan>) of
  `{content, priority, status}`. Zed also implements an unstable v2
  `plan_update` keyed by `planId`, so a session can hold several plans
  (`zed: crates/agent_servers/src/acp/session_updates.rs:235-256`);
  [unconfirmed] in ACP's published docs.

## 6. Synthesis

### Kinds worth a side View

| Kind | Codex source | Claude source | Shape | Key | Outlives the turn? |
|---|---|---|---|---|---|
| **Checklist** | `turn/plan/updated`; disk: `update_plan` args | `TodoWrite`/`Task*` calls, or the host's own tool | Whole (Codex, `TodoWrite`) or changes (`Task*`) | Codex: thread (latest wins); Claude: task list id | Yes, until replaced |
| **Proposed plan** | `Plan` item | `ExitPlanMode` and the plan file | Streamed text, then whole | Item id / plan file | Yes; it's content, closer to a Block |
| **Subagent** | `collabAgentToolCall`, `subAgentActivity`, child thread | `task_*` with `local_agent`, sidechain file | Lifecycle changes | Child thread id / agent id, plus the spawning tool call id | Background ones, yes |
| **Background terminal / task** | open `commandExecution` item; `backgroundTerminals/list` | `task_*` with `local_bash`/monitor; `background_tasks_changed`; `get_task_output` | Lifecycle changes + output stream | Call id + process id / task id | Yes, until exit or session end |
| **Pending approval or question** | server requests + `serverRequest/resolved` | `can_use_tool`, `requires_action` | Open/closed pair | Request id + item/tool call id | Until answered |
| **Goal / wakeups / queue** | `thread/goal/*`, `thread/queue/*` | `CronCreate`, `ScheduleWakeup` | Whole snapshots | Thread / cron id | Yes |

### Mapping to AG-UI

AG-UI 1.0 definitions (`agui: types.ts`):

- `STATE_SNAPSHOT {snapshot}` and `STATE_DELTA {delta: JsonPatch}` (lines
  723-757, 904-940): one JSON document per run that "persists across runs
  on a thread". There is no per-subagent state; `subagentRunId` only records
  who made a change (`agui: docs/concepts/subagents.mdx:170-195`).
- `ACTIVITY_SNAPSHOT {messageId, activityType, content, replace?}` and
  `ACTIVITY_DELTA {messageId, activityType, patch}` (lines 1313-1413): an
  `ActivityMessage` with role `activity`, **placed inline** where it first
  arrives, updated in place, and stripped from input sent back to the agent.
- `SUBAGENT_STARTED {subagentRunId, name, description?, parentSubagentRunId?, parentToolCallId?, parentMessageId?}`,
  `SUBAGENT_FINISHED {subagentRunId, result?, outcome?: success|suspended}`,
  `SUBAGENT_ERROR {subagentRunId, message, code?}` (lines 2369-2530). Other
  events carry an optional `subagentRunId`.

| Kind | Best AG-UI fit | Fit |
|---|---|---|
| Checklist (whole list) | `StateSnapshot` under a key such as `/checklist`, or an `ActivitySnapshot` of type `checklist` | Good for Codex and `TodoWrite`. `Task*` changes map to `StateDelta` JSON Patch once the producer has built the list. A Codex child's checklist would need a key per thread |
| Proposed plan | A message or Block, not side state | Outside this note |
| Subagent | `SubagentStarted`/`Finished`/`Error`, `parentToolCallId` = spawning call; progress as `ActivityDelta` | Fits Claude, where child messages share the stream. **Codex children are separate threads**; AG-UI has no child thread id, so following one means a second subscription that AG-UI cannot name |
| Background terminal / task | `ActivitySnapshot` keyed by the call id, `ActivityDelta` for status | Partial: activity is inline and per run. A shell that outlives its run, or one started by Ctrl+B, has no run to belong to |
| Pending approval | AG-UI interrupts (settled on #683) | Good; the "open" list is the set of unresolved interrupts |
| Goal, wakeups, queue | `StateSnapshot` keys | Workable, but a `Custom` event may be clearer |

### What's missing, for our protocol to add

1. **A list of live side state on attach.** AG-UI's transport is per run. A
   watcher joining a running thread needs the current checklist, running
   work and open approvals at once. Codex gives part of this (pending
   requests re-sent, `backgroundTerminals/list`); Claude gives
   `background_tasks_changed`, which misses foreground work. The producer
   has to keep the registry itself.
2. **State that outlives a run.** Background shells and agents keep running
   after `RunFinished`, and their completions arrive in later turns. AG-UI
   ties activity and subagents to a run.
3. **Child threads.** A way to name a child thread (Codex thread id; Claude
   agent id with its own transcript) so a side View can open it as its own
   conversation, as every app surveyed does.
4. **A key per kind.** `StateSnapshot` is one untyped document. Side Views
   need to bind to one part (checklist, tasks, approvals), so we need either
   agreed JSON Pointer roots or a typed event per kind.
5. **Replay from disk.** For the session-log path, the checklist and tasks
   must be rebuilt from tool calls: Codex `update_plan` arguments, Claude
   `Task*`/`TodoWrite` results, `Agent`/`Bash` results plus
   task notifications. The live-only frames (Codex `PlanUpdate`, Claude
   `task_*`) are not in the logs.
6. **Ownership by agent.** T3 Code's rule is that a child's checklist nests
   under the child and never replaces the root's. AG-UI's state has no
   per-subagent scope, so attribution needs a key.
