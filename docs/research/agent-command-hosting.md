# Research: how agent harnesses run commands and touch files, and where a host can supply its own

Status: research notes, 8 November 2026. Nothing here is decided.

Question: [#731](https://github.com/flotilla-org/wheelhouse/issues/731), for
the map [#686](https://github.com/flotilla-org/wheelhouse/issues/686). What
does each agent harness expect of the thing that runs its commands and reads
and writes its files? Where can a host such as Wheelhouse supply its own: a
shell hosted in a Cleat session, so a person can watch every command and TUI,
or file tools that reach a workspace while the agent loop runs somewhere else?

**Framing.** Every option below is progressive enhancement. Stock tools, tools
augmented by hooks, substituted tools and, one day, a loop of our own can sit
side by side and be compared. Nothing here picks "the" way. "The model is
trained on its own tools" is listed as a hypothesis to measure, not a verdict.

**Sources.** All read on 8 November 2026 at these revisions:

| Prefix | Source | Revision |
| --- | --- | --- |
| `claude-sdk:` | `@anthropic-ai/claude-agent-sdk` from npm, `sdk.d.ts` and `sdk-tools.d.ts` | 0.3.319 |
| `cc-docs:` | Claude Code and Agent SDK docs at code.claude.com: [tools reference](https://code.claude.com/docs/en/tools-reference), [hooks](https://code.claude.com/docs/en/agent-sdk/hooks), [custom tools](https://code.claude.com/docs/en/agent-sdk/custom-tools) | fetched 2026-11-08 |
| `codex:` | [openai/codex](https://github.com/openai/codex), paths under `codex-rs/` | tag `rust-v0.162.1` |
| `codex-bin:` | strings in the installed Codex binary (`codex-cli 0.162.1`, `@openai/codex-linux-x64`) | 0.162.1 |
| `pi:` | [earendil-works/pi](https://github.com/earendil-works/pi) (the old `badlogic/pi-mono` redirects here) | `415be82` |
| `ai:` | [vercel/ai](https://github.com/vercel/ai), `ai` 7.0.165 | `0f3a85f` |
| `acp:` | [agentclientprotocol/agent-client-protocol](https://github.com/agentclientprotocol/agent-client-protocol), `docs/` | `7441d10` |
| `caa:` | [agentclientprotocol/claude-agent-acp](https://github.com/agentclientprotocol/claude-agent-acp) (formerly `zed-industries/claude-code-acp`); `caa17:` is tag `v0.17.1` | `3b22586` |
| `cxa:` | [zed-industries/codex-acp](https://github.com/zed-industries/codex-acp) | `296069e` |
| `zed:` | [zed-industries/zed](https://github.com/zed-industries/zed) | `b5cd310` |
| `gem:` | [google-gemini/gemini-cli](https://github.com/google-gemini/gemini-cli) | `62bab5b` |
| `cleat:` | [flotilla-org/cleat](https://github.com/flotilla-org/cleat) `origin/main`, and the installed `cleat` binary built from it | `ac43c37` (2026-11-01) |
| `wh:` | this repo: `tools/codex-agent-host.mjs` on `main` (`38065a3`); `tools/claude-agent-host/claude-agent-host.mjs` on `feat/696-claude-producer` (`8d7e13b`, PR #730) | as stated |

The Claude SDK ships compiled JavaScript, so its type declarations and doc
comments are the primary source, alongside Anthropic's docs. Line numbers in
`claude-sdk:` refer to 0.3.319. Codex, pi, AI SDK and ACP line numbers are at
the revisions above. Anything I could not confirm is marked **[unverified]**.
All examples are synthetic.

## Summary

| | Claude Agent SDK | Codex app-server | pi / pi durable | Vercel AI SDK | ACP v1 client methods |
| --- | --- | --- | --- | --- | --- |
| **Shell state between calls** | Working directory only, inside the project roots. Env does not persist; startup-file aliases and functions are captured once and reapplied. | Nothing. Each `exec_command` is a new process. State lives only inside one running session driven by `write_stdin`. | Nothing. Fresh `spawn` per call; cwd is always the session cwd. | Whatever the host's tool does. The sandbox contract says cwd does not persist. | Each `terminal/create` names its own `cwd` and `env`. |
| **Foreground timeout** | Default 2 min, max 10 min; on timeout the command moves to the background instead of dying. | `yield_time_ms` (default 10 s, 250 ms to 30 s): the call returns, the process keeps running. | `timeout` in seconds, no default; kills the process tree. | Host's choice. | None in the protocol; the agent races a timer against `terminal/wait_for_exit`, then `terminal/kill`. |
| **Output back to the model** | Inline up to ~30,000 chars; past that, a file path and a 2,000-char preview. Failures get a 10,000-char head-and-tail excerpt. | Head and tail kept to 1 MiB; model sees it trimmed from the middle to `max_output_tokens` (default 10,000 tokens). | Last 2,000 lines or 50 KB; full output spilled to a temp file whose path is in the result. | Host's choice; `execute` may be an async generator for preliminary results. | `outputByteLimit`, truncated from the start. |
| **Background commands** | `run_in_background`; completion is pushed as `task_notification`, and can start a new turn. | Every session that outlives `yield_time_ms` is a background terminal. The model polls with `write_stdin`; nothing tells it when one exits. | None. | Host's choice. | A terminal runs in the background by definition; no notification beyond `wait_for_exit`. |
| **Interactive input / TUIs** | None in Bash (stdin not exposed to the model) **[unverified: PTY or pipes]**. | `tty: true` gives a PTY; `write_stdin` sends keystrokes. | None (stdin closed). | `Experimental_SandboxSession.spawn({ stdin: true })` exposes a writable stdin to tools. | None. Explicitly no input method. |
| **File-tool hidden state** | Read-before-edit via a `readFileState` cache with mtime staleness; checkpoints and `rewindFiles`. | No read-before-edit; `apply_patch` grammar; per-turn diff; no undo. | No read-before-edit; per-file mutation queue. | None (host's). | Client may return unsaved editor buffers. |
| **Rewrite a call's input** | `PreToolUse` → `updatedInput`; also `canUseTool` → `updatedInput`. | Approvals cannot rewrite. A config-level `PreToolUse` hook can (`updated_input`). | `tool_call` event mutates `event.input` in place. | `prepareStep`, or the host's `execute`. | n/a |
| **Replace a tool** | `tools` / `disallowedTools` + `createSdkMcpServer`; `toolAliases` reroutes the old name. | Dynamic tools cannot reuse built-in names; `features.shell_tool=false` removes the shell. | `pi.registerTool()` with the same name replaces it; `--tools`, `--no-builtin-tools`. | All tools are the host's. | n/a |
| **Run tools elsewhere** | `spawnClaudeCodeProcess` moves the whole CLI. | `environment/add` + `thread/start.environments`: a host-run exec-server runs `exec_command` and `apply_patch`. Experimental. | `*Operations` interfaces (pi); `ExecutionEnv` (pi durable); `pi-env` remote daemon. | `experimental_sandbox`. | The client is the elsewhere: `terminal/*` and `fs/*`. Being removed in v2. |

## Claude Agent SDK

### Commands

- **Input.** Bash takes `command`, `timeout` (ms, max 600000 in the
  foreground), `description`, `run_in_background` and
  `dangerouslyDisableSandbox` (`claude-sdk:sdk-tools.d.ts:805-838`).
- **What persists.** "The Bash tool runs each command in a separate process."
  A `cd` carries over while it stays inside the project or an added working
  directory; outside them the cwd resets with `Shell cwd was reset to <dir>`.
  Subagents never carry it over. `CLAUDE_BASH_MAINTAIN_PROJECT_WORKING_DIR=1`
  turns the carry-over off. "Environment variables don't persist." Aliases,
  functions and shell options from `~/.zshrc`/`~/.bashrc`/`~/.profile` are
  captured at session start and applied to every command. `CLAUDE_ENV_FILE`
  or a `SessionStart` hook can make variables persist
  (`cc-docs:` tools reference, "What persists between commands"). A
  `CwdChanged` hook reports `old_cwd`/`new_cwd` (`claude-sdk:sdk.d.ts:683-691`).
- **Foreground timeout.** `BASH_DEFAULT_TIMEOUT_MS` (2 min) and
  `BASH_MAX_TIMEOUT_MS` (10 min). A command that reaches its timeout is moved
  to the background, not stopped, unless it starts with `sleep`; the result
  says `Command did not complete within its 120s timeout and was moved to the
  background` with a task id and output file. A `cd` inside a moved command
  never carries over (`cc-docs:` "Timeout and output limits", "Foreground
  commands that move to the background"; `BashOutput.timedOutAfterMs` and
  `backgroundCwdHint`, `claude-sdk:sdk-tools.d.ts:3277-3320`).
- **Output.** Output streams to a working file (killed past 5 GB). A valid
  result is inline up to about 30,000 characters; past that the model gets the
  path of a saved file plus a preview of up to 2,000 characters. A failure is
  inline up to about 10,000 characters, as a head-and-tail excerpt with no
  path. `bashOutputMaxChars` (4,000–128,000) sets the inline ceiling
  (`cc-docs:` "Output limits"; `claude-sdk:sdk.d.ts:7767`). The structured
  output carries `stdout`, `stderr`, `interrupted`, `persistedOutputPath`,
  `persistedOutputSize`, `backgroundTaskId` and a client-only `gitOperation`
  (`claude-sdk:sdk-tools.d.ts:3277-3386`).
- **PTY or pipes.** Not stated in the types or the docs I read
  **[unverified]**. Neither exposes stdin to the model.

### Asynchronous commands

- `run_in_background: true` starts a background task; with it, `timeout` is
  the time limit there (default 30 min, max 2 h in unattended sessions such
  as an SDK app; none in an interactive terminal session)
  (`claude-sdk:sdk-tools.d.ts:830-833`; `cc-docs:` "Time limit for background
  commands").
- Completion is pushed, not polled. The SDK emits `task_started`,
  `task_progress`, `task_updated` and `task_notification` system messages
  (`claude-sdk:sdk.d.ts:6428-6560`), plus `background_tasks_changed` as a
  level signal (`sdk.d.ts:3843-3847`). `task_notification` carries `status`
  (`completed`/`failed`/`stopped`), `output_file` and `summary`.
- The host can push a foreground command into the background with
  `Query.backgroundTasks(toolUseId?)` and stop one with `stopTask(taskId)`
  (`claude-sdk:sdk.d.ts:3346-3364`); the model has `TaskStop`
  (`sdk-tools.d.ts:977-986`).
- That a background completion starts a new turn is the #696 observation;
  the docs say the same of Monitor ("Claude interjects when an event
  arrives") (`cc-docs:` "Monitor tool"). `BashOutput.backgroundWakesOwner`
  and `backgroundEndsWithFinalResponse` say when a completion can and cannot
  follow (`claude-sdk:sdk-tools.d.ts:3317-3321`).
- **Monitor** runs a command or WebSocket in the background and feeds each
  stdout line or frame to the model as an event; deadline 5 min by default,
  at most 30 min (`claude-sdk:sdk-tools.d.ts:2968-2988`; `cc-docs:` "Monitor
  tool").
- **Interactive input and TUIs:** no tool for them. Today an agent reaches a
  TUI through Bash plus Cleat (the cleat-sessions skill), as below.

### File tools and their hidden state

- **Read-before-edit.** Edit requires that the file was read in this
  conversation, and a `PARTIAL view` read does not count. Opus 4.6, Haiku 4.5
  and older always require it; newer models may edit an unread file when
  reading it would not prompt and the Read tool is available. Viewing a file
  with a plain single-file `cat`, `head`, `tail`, `sed -n`, `grep` or `rg`
  through Bash also counts. Write has the same rules for existing files
  (`cc-docs:` "Edit tool behavior", "Write tool behavior").
- **The cache behind it.** The CLI keeps a `readFileState` cache of path and
  mtime. `Query.seedReadState(path, mtime)` seeds it "so a subsequent Edit
  won't fail 'file not read yet'", and a file changed since that mtime still
  needs a fresh Read (`claude-sdk:sdk.d.ts:3258-3267`, `5360-5368`). Bash
  results carry `staleReadFileStateHint` listing cached entries whose mtime a
  command bumped (`sdk-tools.d.ts:3349`).
- **Side effects of reading.** Viewing a file loads any subdirectory
  `CLAUDE.md` and path-scoped rules for it (`cc-docs:` "Edit tool behavior").
- **Diffs and checkpoints.** Edit returns `originalFile`, `structuredPatch`
  and an optional `gitDiff` (`claude-sdk:sdk-tools.d.ts:3414-3462`).
  `enableFileCheckpointing` backs files up before they change, and
  `Query.rewindFiles(userMessageId, { dryRun })` restores them
  (`sdk.d.ts:1768-1775`, `3246-3256`). Whether checkpoints cover edits made by
  Bash or by an MCP tool **[unverified]**.
- **What breaks if file tools run elsewhere.** If Read is replaced but the
  built-in Edit kept, Edit's read check finds nothing in `readFileState`
  unless the host seeds it. If all three are replaced, the replacements carry
  no read check, no checkpoint and no `CLAUDE.md` loading unless the host adds
  them. The Bash-as-read shortcut only feeds the built-in cache. (Inferred
  from the facts above.)

### Where a host can step in

- **Rewrite input.** `PreToolUse` returns
  `hookSpecificOutput.updatedInput` (`claude-sdk:sdk.d.ts:2862-2868`). Pair it
  with `permissionDecision: 'allow'` or `'ask'`, or omit the decision and the
  rewrite still goes through normal permission evaluation; `'defer'` drops it
  (`cc-docs:` hooks, "Modify tool input"). Hooks run in parallel, so two
  rewriting hooks race (`cc-docs:` "Register multiple hooks"). `canUseTool`
  can also return `{ behavior: 'allow', updatedInput }`
  (`claude-sdk:sdk.d.ts:2620-2624`), as can a `PermissionRequest` hook
  (`2608-2614`).
- **Rewrite output.** `PostToolUse` → `updatedToolOutput`, for any tool
  (`claude-sdk:sdk.d.ts:2790-2794`; `cc-docs:` hooks, "Outputs").
- **Defer a call.** `permissionDecision: 'defer'` ends the turn with
  `stop_reason: "tool_deferred"` so the call can be resumed later
  (`claude-sdk:sdk.d.ts:1034`; `cc-docs:` hooks, "Outputs"). Whether a host
  could run the call itself in between and supply the result **[unverified]**.
- **Turn a tool off.** `tools: [...]` lists the built-ins to keep (`[]` for
  none); a bare name in `disallowedTools` removes the tool from context
  (`claude-sdk:sdk.d.ts:1680-1685`, `1713-1725`; `cc-docs:` custom tools,
  "Configure allowed tools").
- **Register a replacement.** `createSdkMcpServer({ name, tools })` with
  `tool(name, description, zodShape, handler)` runs in-process; the model sees
  `mcp__{server}__{tool}`. Tool calls are bounded by the server's `timeout` or
  `MCP_TOOL_TIMEOUT`, "effectively unbounded by default"
  (`claude-sdk:sdk.d.ts:640-665`, `10316`; `cc-docs:` custom tools).
- **Reroute the old name.** `toolAliases: { Bash: 'mcp__workspace__bash' }`
  sends a model-emitted `Bash` call to the MCP tool. The doc comment names
  this exact case, "a host that runs Bash inside a remote sandbox via an MCP
  tool" (`claude-sdk:sdk.d.ts:1686-1711`).
- **Run the whole harness elsewhere.** `spawnClaudeCodeProcess(options)`
  receives `{ command, args, cwd, env, signal }` and returns a
  `SpawnedProcess` (stdin, stdout, `kill`, `exit`), "to run Claude Code in
  VMs, containers, or remote environments" (`claude-sdk:sdk.d.ts:2541-2558`,
  `10029-10110`). A prewarmed host that spawns remotely must pass a `cwd` that
  exists there (`sdk.d.ts:2876-2887`). Every stock tool then acts where the
  CLI runs.
- **Sandbox.** `sandbox?: SandboxSettings` (`claude-sdk:sdk.d.ts:2296`,
  `3594-3600`) constrains the stock Bash; not a hosting seam.

### What we do today

The Claude producer passes `cwd`, `permissionMode`, `settingSources` and
`canUseTool`, and changes no tools (`wh:claude-agent-host.mjs:520-525`). It
already maps `task_*` messages for `local_bash` and `monitor` tasks into the
Agent View's task list (`wh:claude-agent-host.mjs:165-182`).

## Codex app-server

### Commands

- **Tools.** In 0.162.1 the model's command tools are `exec_command` and
  `write_stdin` (unified exec). The older `shell` and `shell_command` types
  deserialize to unified exec (`codex:protocol/src/openai_models.rs:316-320`;
  `core/src/tools/spec_plan.rs:1150-1198`). Flags `shell_tool`,
  `unified_exec` and `unified_exec_tty` are stable and on
  (`codex:features/src/lib.rs:1011-1044`); a user setting cannot turn
  `unified_exec` off, only managed requirements can
  (`core/src/config/managed_features.rs:177-183`).
- **`exec_command` input:** `cmd`, `workdir`, `tty` (default `false`, so
  pipes; the description still says "Runs a command in a PTY"),
  `yield_time_ms` (default 10,000, clamped 250–30,000), `max_output_tokens`
  (default 10,000), `shell`, `login`, `environment_id` when several
  environments exist, and approval fields such as `sandbox_permissions` and
  `justification` (`codex:core/src/tools/handlers/shell_spec.rs:24-115`,
  `232-278`; `core/src/unified_exec/mod.rs:73-79`, `218-225`).
- **What persists.** Each call runs `[shell, -lc|-c, cmd]`: nothing carries
  over except inside one live session (`codex:core/src/shell.rs:24-33`). Every
  command gets `NO_COLOR=1`, `TERM=dumb`, `PAGER=cat` and similar
  (`core/src/unified_exec/process_manager.rs:93-104`). A shell snapshot of the
  user's environment is taken once per session for local environments
  (`core/src/shell_snapshot.rs:151-175`); what it restores **[unverified]**.
- **Output.** A 1 MiB head-and-tail buffer, middle dropped with `... N bytes
  omitted ...`; the model sees it trimmed from the middle to
  `max_output_tokens` with a `Warning: truncated output` prefix. The reply
  begins `Chunk ID`, `Wall time`, then `Process exited with code N` or
  `Process running with session ID N`
  (`codex:core/src/unified_exec/head_tail_buffer.rs:5-19`;
  `core/src/tools/context.rs:482-547`; `codex-bin:` "Process running with
  session ID").

### Asynchronous commands

- **Polling.** A process still running at `yield_time_ms` returns a
  `session_id`. `write_stdin { session_id, chars, yield_time_ms,
  max_output_tokens }` writes and reads; empty `chars` polls, waiting 5 s up to
  `background_terminal_max_timeout` (default 300 s)
  (`codex:core/src/tools/handlers/shell_spec.rs:117-159`;
  `process_manager.rs:1014-1021`).
- **Completion is not pushed to the model.** The exit watcher emits an
  `ExecCommandEnd` event to the client only; the model learns of an exit from
  a later `write_stdin` reply (`codex:core/src/unified_exec/async_watcher.rs:155-241`).
- **Interactive input.** With `tty: true`, `write_stdin` sends keystrokes;
  without one, stdin is closed and only `"\u{3}"` (interrupt) is accepted
  (`codex:process_manager.rs:983-989`).
- **Limits.** Soft cap of 64 processes, least recently used pruned, 8 newest
  protected (`codex:core/src/unified_exec/mod.rs:82`;
  `process_manager.rs:1722-1793`). Sessions outlive the turn; a host can list
  and end them with `thread/backgroundTerminals/{list,terminate,clean}`,
  experimental (`codex:app-server-protocol/src/protocol/common.rs:749-766`;
  `codex-bin:`). Whether any abort path besides shutdown and clean kills them
  **[unverified]**.

### File tools

- `apply_patch` is a freeform tool with a Lark grammar (`*** Begin Patch`,
  `*** Update File:`, `@@`, `*** End Patch`); offered per the model catalog,
  and no config key turns it off
  (`codex:core/src/tools/handlers/apply_patch_spec.rs:5-28`;
  `core/src/tools/spec_plan.rs:1344-1347`). An `exec_command` whose command
  parses as an apply_patch call is applied as a patch
  (`core/src/tools/handlers/exec_command.rs:372-397`).
- No read-before-edit rule found. Per-turn diffs reach the client as
  `turn/diff/updated` (`codex:core/src/tools/events.rs:620-648`); whether
  shell-made changes feed it **[unverified]**. Ghost-commit undo is removed;
  `thread/revert` rewinds history only, "It does not revert local file
  changes" (`codex:features/src/lib.rs:1005-1008`;
  `app-server-protocol/src/protocol/v2/thread.rs:1273-1282`).
- Patches are checked and applied through the selected environment's
  filesystem, local or remote (`codex:core/src/tools/handlers/apply_patch.rs:333-344`).

### Where a host can step in

- **Dynamic tools** (experimental; needs `initialize.capabilities.experimentalApi`):
  `thread/start.dynamicTools` declares `{ type: "function", name,
  description, inputSchema }` or namespaces; calls arrive as the server
  request `item/tool/call { threadId, turnId, callId, tool, arguments }` and
  the client replies `{ contentItems, success }`
  (`codex:app-server-protocol/src/protocol/v2/thread.rs:145-151`;
  `protocol/src/dynamic_tools.rs:13-43`; `v2/item.rs:1663-1692`;
  `codex-bin:` `dynamicTools`, `item/tool/call`). **They cannot replace a
  built-in:** a clashing name is skipped, and `exec_command` and
  `shell_command` are reserved ("skipping external tool with reserved name")
  (`codex:core/src/tools/registry.rs:394-416`; `codex-bin:`).
- **Turn the shell off.** `features.shell_tool=false` removes `exec_command`
  and `write_stdin` (`codex:core/src/tools/spec_plan.rs:1150-1198`). Whether
  `thread/start.config` accepts `features.*` keys **[unverified]**; the CLI
  takes `-c features.<name>=…`.
- **Approvals cannot rewrite.** `item/commandExecution/requestApproval` (kind
  `command` or `writeStdin`) and `item/fileChange/requestApproval` answer with
  a decision only (`codex:app-server-protocol/src/protocol/v2/item.rs:66-124`,
  `1555-1658`). A config-level `PreToolUse` hook (stable) can replace `cmd`
  through `updated_input`, outside the app-server protocol
  (`codex:core/src/tools/handlers/exec_command.rs:527-546`;
  `hooks/src/events/pre_tool_use.rs:44`).
- **Environments** (experimental). `environment/add { environmentId,
  execServerUrl, authBearerToken? }`, then `thread/start.environments: [{
  environmentId, cwd }]`; the client sees `thread/environment/connected` and
  `/disconnected` (`codex:app-server-protocol/src/protocol/v2/thread.rs:136-144`;
  `common.rs:1213-1231`; `codex-bin:` `environment/add`,
  `thread/environment/connected`). An environment is a `codex exec-server`
  speaking JSON-RPC over WebSocket: `process/start { processId, argv, cwd,
  env, tty, pipeStdin }`, `process/read`, `process/write`, `process/signal`,
  `process/terminate`, notifications `process/output` and `process/exited`,
  and `fs/readFile`, `fs/writeFile`, `fs/readDirectory`, `fs/remove` and more
  (`codex:exec-server-protocol/src/protocol.rs:25-59`;
  `exec-server/README.md`). A host that speaks this protocol runs Codex's own
  `exec_command` and `apply_patch` on its side, with the model's tools
  unchanged.
- **Client-run commands** (not model tools): `command/exec` with `tty`,
  `streamStdin`, `cwd`, `env`, `size`, and follow-ups `/write`, `/resize`,
  `/terminate` (stable); `thread/shellCommand` (stable, unsandboxed)
  (`codex:app-server-protocol/src/protocol/v2/command_exec.rs:30-101`;
  `v2/thread.rs:1176-1188`).
- **What a UI sees.** `item/started`/`item/completed` with a
  `commandExecution` item (`command`, `cwd`, `processId`, `source`,
  `aggregatedOutput`, `exitCode`); `item/commandExecution/outputDelta`;
  `item/commandExecution/terminalInteraction { processId, stdin }` for each
  `write_stdin` (`codex:app-server-protocol/src/protocol/v2/item.rs:295-326`,
  `1500-1516`; `codex-bin:`). For a background session `item/completed`
  arrives whenever it exits, possibly after the turn.

### What we do today

The Codex bridge starts a thread with `{ cwd }` only, streams `outputDelta`,
and answers approvals; it registers no dynamic tools and no environment
(`wh:tools/codex-agent-host.mjs:239-267`, `446-449`).

## pi and pi durable

**Identification.** "pi" is the coding agent `@earendil-works/pi-coding-agent`
in [earendil-works/pi](https://github.com/earendil-works/pi); the old
`badlogic/pi-mono` repo redirects there (checked with `gh api`). "pi durable"
is `@earendil-works/pi-durable` in the same repo, `packages/durable`, marked
"Experimental. The API changes without notice between releases"
(`pi:packages/durable/README.md:1-5`). I am confident of both.

### Commands and files (pi)

- **Tools.** `read`, `bash`, `edit`, `write` by default; also `grep`,
  `find`, `ls` (`pi:packages/coding-agent/docs/cli.md:121-158`).
- **bash.** `{ command, timeout? }` (seconds, no default). Each call spawns
  afresh with `cwd` set to the session cwd, so `cd` does not persist; env is
  rebuilt each call plus `PI_SESSION_ID` and friends; stdin is closed; abort
  or timeout kills the process tree
  (`pi:packages/coding-agent/src/core/tools/bash.ts:21-42`, `95-240`,
  `297-303`).
- **Output.** The last 2,000 lines or 50 KB; full output goes to a temp file
  named in the result and in `details.fullOutputPath`; updates stream through
  `onUpdate` (`pi:.../tools/truncate.ts:11-12`; `bash.ts:310-384`).
- **No background tool** found in the source (inference; the README says pi
  skips sub-agents and plan mode, `pi:packages/coding-agent/README.md:19`).
- **edit** takes `{ path, edits: [{ oldText, newText }] }`, matched uniquely
  against the original file; writes to a file are serialized by
  `withFileMutationQueue`. No read-before-edit check found
  (`pi:.../tools/edit.ts:18-38`, `146`).

### Where a host can step in (pi)

- **Operations.** `BashOperations.exec(command, cwd, { onData, signal,
  timeout, env }) → { exitCode }`, and `ReadOperations`, `WriteOperations`,
  `EditOperations`, `LsOperations`, `FindOperations`, passed to
  `createBashTool(cwd, { operations, commandPrefix, spawnHook })` and the
  other factories (`pi:packages/coding-agent/docs/extensions.md:2391-2449`;
  `bash.ts:74-93`, `242-253`). Examples `examples/extensions/ssh.ts` and
  `gondolin/index.ts`. Grep always runs local `rg`; remote grep needs
  `execute` overridden (`extensions.md:2433`).
- **Replacement.** `pi.registerTool()` with a built-in's name replaces it;
  the result must keep the same shape (`extensions.md:2358-2389`). CLI flags
  `--tools`, `--exclude-tools`, `--no-builtin-tools` (`cli.md:121-130`).
- **Events.** `tool_call` can mutate `event.input` in place or return `{
  block: true }`; `tool_result` can patch the result; `user_bash` can route
  the user's `!` commands to other `operations`
  (`extensions.md:973-1108`).
- **Embedding.** SDK `createAgentSession({ tools, customTools, … })`
  (`pi:docs/sdk.md:96-118`); RPC mode `pi --mode rpc` over JSONL, which does
  not let the client supply tool implementations (`docs/rpc.md:1-60`). No ACP
  mode in the repo.

### pi durable

- Every turn, tool call and piece of state is committed to storage before it
  is shown; `resume()` continues after a crash. A tool call's intent is
  committed before `execute()`; after a crash only tools declared `replay:
  "safe"` rerun, others give the model an `interrupted` result
  (`pi:packages/durable/README.md:115-138`, `245-330`).
- **Host seam: `env`**, a per-call factory `({ cwd, conversationId }) =>
  ExecutionEnv`, where `ExecutionEnv = FileSystem & Shell`. `Shell.exec`
  takes `{ cwd, env, timeout, onOutput, spill: { afterBytes, afterLines },
  window: { maxBytes, maxLines, … } }` and returns `{ exitCode, spillPath? }`;
  `FileSystem` has `readTextFile`, `writeFile`, `listDir`, `watch` and more
  (`pi:packages/durable/README.md:430-463`; `src/env/index.ts:212-377`).
- **`pi-env`** is a remote `ExecutionEnv` that talks to a small Rust daemon
  deployed over SSH (`pi:packages/env/README.md:1-30`).
- **Watching.** `conversation.watch()` streams committed view operations;
  `docs["pi.live"]` holds the running generation and tool output, committed at
  most every 100 ms (`pi:packages/durable/README.md:477-527`).

## Vercel AI SDK

A library, not a harness: every tool is the host's.

- `tool({ description, inputSchema, execute })`; `execute` may be an async
  generator whose yields are preliminary results; `toolApproval` replaces the
  deprecated `needsApproval`; `prepareStep`, `activeTools`, `stopWhen`
  (`isStepCount`, `hasToolCall`); `ToolLoopAgent`; MCP through
  `createMCPClient` from `@ai-sdk/mcp`
  (`ai:content/docs/03-ai-sdk-core/15-tools-and-tool-calling.mdx:6-50`,
  `159-265`, `580-600`, `778-810`, `1383-1402`;
  `03-agents/04-loop-control.mdx:21-34`; `16-mcp-tools.mdx:46`).
- **Provider-defined tools the host executes:** `anthropic.bash_20250124 {
  command, restart? }`, `anthropic.text_editor_20250728 { command:
  view|create|str_replace|insert, … }`, `openai.local_shell`, `openai.shell
  { commands, timeoutMs?, maxOutputLength? }`, `openai.apply_patch`
  (`ai:packages/anthropic/src/tool/bash_20250124.ts:26-37`;
  `text-editor_20250728.ts:18-81`; `packages/openai/src/tool/local-shell.ts`,
  `shell.ts`, `apply-patch.ts`). These are the schemas each vendor's model was
  trained on, run by our code: a direct way to test the trained-tools
  hypothesis.
- **Sandbox seam** (experimental): `experimental_sandbox` is forwarded to
  each `execute`. `Experimental_SandboxSession` has `readTextFile`,
  `writeTextFile`, `listDirectory`, `run` and `spawn({ command,
  workingDirectory, env, stdin })` returning stdout/stderr streams, `wait()`,
  `kill()` and a writable `stdin`. Its doc says cwd does not persist between
  commands (`ai:packages/provider-utils/src/types/sandbox.ts:155-334`).
  `@ai-sdk/tool-bash` builds a bash tool on a sandbox.
- **`@ai-sdk/harness`** wraps whole harnesses (`harness-pi`,
  `harness-claude-code`, `harness-codex`, `harness-acp`) against a supplied
  sandbox (`ai:architecture/harness-abstraction.md:40-48`). `harness-pi`
  implements pi's `exec`, `readBuffer`, `writeFile` over the sandbox, which
  shows pi's Operations seam is enough for remote execution
  (`ai:packages/harness-pi/src/pi-remote-ops.ts:243-260`, `667-690`).

## ACP client `terminal/*` and `fs/*`

### v1 (stable)

- Capabilities `clientCapabilities.fs.{readTextFile, writeTextFile}` and
  `clientCapabilities.terminal`; absent means the agent MUST NOT call them
  (`acp:docs/protocol/v1/file-system.mdx:6-29`; `terminals.mdx:8-26`).
- `fs/read_text_file { sessionId, path, line?, limit? }` may return unsaved
  editor contents; `fs/write_text_file { sessionId, path, content }`
  (`file-system.mdx:33-126`).
- `terminal/create { sessionId, command, args?, env?, cwd?, outputByteLimit?
  } → { terminalId }`, run in the background, truncated from the start;
  `terminal/output → { output, truncated, exitStatus? }`;
  `terminal/wait_for_exit`; `terminal/kill`; `terminal/release`. Timeouts are
  the agent's: race a timer against `wait_for_exit`, then `kill`
  (`terminals.mdx:28-282`).
- **No input.** No write or resize method; clients SHOULD neutralize control
  sequences so output cannot "send input or query responses"
  (`terminals.mdx:284-289`).
- A tool call embeds `{ type: "terminal", terminalId }` for live output
  (`acp:docs/protocol/v1/tool-calls.mdx:316-335`).

### v2 (draft RFDs)

- **Removes** `clientCapabilities.fs`, `terminal`, every `fs/*` and
  `terminal/*` method: "it has not been widely adopted … most stuck to their
  standard implementations" (`acp:docs/rfds/v2/client-filesystem-terminal-capabilities.mdx`,
  read directly).
- **Agent-owned, display-only terminals:** `terminal_update` (command, cwd,
  size, base64 output snapshot, exit status) and `terminal_output_chunk`; no
  input, resize or wait from the client (`acp:docs/rfds/v2/terminal-output.mdx`).
  `background-work` adds `terminal/cancel` and a background list.

### Who uses them

- **claude-agent-acp before 0.18** registered an in-process MCP server with
  `mcp__acp__Read`, `Write`, `Edit`, `Bash`, `BashOutput`, `KillShell`, and
  disallowed the built-ins; its Bash called `terminal/create` with
  `outputByteLimit: 32000` and embedded the terminal; it restated
  read-before-edit in prompt text (`caa17:src/mcp-server.ts:118-458`;
  `src/acp-agent.ts:1168-1190`). This is the Claude "turn off and replace"
  pattern in production.
- **From 0.18.0** it switched back: "it won't use client capabilities for
  files or terminals" (`caa:CHANGELOG.md:1300-1303`). Bash output now goes
  out as display-only `_meta.terminal_output` when the client opts in
  (`caa:src/tool-calls/client-capabilities.ts:18-46`). Why it switched is not
  stated in the changelog lines read **[unverified]**.
- **codex-acp** never calls client fs or terminal methods; it streams
  display-only `_meta.terminal_output` (`cxa:src/thread.rs:1995-2050`,
  `2574-2640`).
- **Gemini CLI** routes file access through `fs/*` when offered, falling back
  to local for paths outside the project (`gem:packages/cli/src/acp/acpFileSystemService.ts:15-80`).
  No terminal use found.

## Prior art: clients that host an agent's terminal or files

- **Zed** advertises `fs` and `terminal`. `terminal/create` makes a real Zed
  terminal through `project.create_terminal_task(...)`, using the remote
  project's shell for SSH projects, with stdin redirected to `/dev/null` and
  pagers disabled (`zed:crates/acp_thread/src/terminal.rs:945-995`;
  `crates/agent_servers/src/acp/v1.rs:491-516`, `10548-10606`). Reads can
  return buffer contents (`v1.rs:10304-10325`). Display-only terminals from
  `_meta.terminal_output` and v2 `terminal_update` feed the same entity
  (`v1.rs:10436-10510`; `acp/terminal_updates.rs:1-45`). That these show as
  terminal cards in the agent panel is inferred from that wiring
  **[unverified: UI code not read]**.
- **CodeCompanion.nvim** advertises `fs` only and handles both methods, with
  a write-rejection hook (`lua/codecompanion/acp/init.lua:887-956` at
  `85d0117`).
- **Codex's own exec-server** and **pi-env** are first-party prior art for
  "the loop here, the commands there".
- **Cleat.** An agent already launches, drives, waits on and captures TUIs
  through its own shell: `cleat launch --cmd`, `send --submit`, `send-keys`,
  `wait --text/--screen-stable/--idle-time/--state`, `capture`, `mark` …
  `transcript --since-marker`, `expect`, `inspect` (where `fg_pgid !=
  leader_pid` means a command is running), and `watch` for a read-only human
  (`cleat:skills/cleat-sessions/README.md:43-59`; `cleat --help` on the
  installed `ac43c37` build). Cleat's VT engine sees OSC 133 prompt marks and
  keeps OSC 7501 program-status records (`cleat:crates/cleat/src/vt/mod.rs:693`;
  `docs/adr/0007-program-status-through-attach.md`). I found no verb that runs
  one command in a session and returns its exit status and output **[unverified
  that none exists]**. The local `~/dev/cleat` clone was at `ce8712a`
  (2026-10-10); I read `origin/main` after fetching.

## Cross-cutting findings

- **No harness keeps a shell between calls.** Claude keeps the cwd; the rest
  keep nothing. A Cleat-hosted persistent shell would change what `export`
  and `source` mean, against what Claude's docs tell users. Whether models
  benefit or trip is a measurement.
- **Completion delivery differs.** Claude pushes (`task_notification`, new
  turn); Codex polls (`write_stdin`); pi and ACP v1 have no background notion
  for the model. A host substituting the shell inherits the harness's
  delivery, or loses it: an MCP `bash` under Claude does not get
  `run_in_background`, auto-backgrounding or `task_*` messages.
- **Interactive input exists only in Codex** (`tty` + `write_stdin`) and the
  AI SDK sandbox contract. Elsewhere TUIs need a separate tool, such as Cleat
  through Bash.
- **Hidden file state lives only in Claude** (`readFileState`, checkpoints,
  `CLAUDE.md` on view). Codex and pi have none to break.
- **The cleanest "elsewhere" seams** are below the tools, where the model's
  tool schemas stay stock: Codex environments (exec-server), pi Operations,
  pi durable `ExecutionEnv`, AI SDK sandbox, and Claude's
  `spawnClaudeCodeProcess` (whole process). ACP v1's client methods were that
  seam for editors and are being withdrawn.

## Options per harness

Each row is an addition to try beside stock tools and compare, not a choice
between them. "Measure" names what would tell them apart.

### Claude Agent SDK

| # | Addition | API | Keeps | Gives up / risks | Measure |
| --- | --- | --- | --- | --- | --- |
| C0 | Stock, observed | `task_*`, tool results | Everything | Nothing watchable live beyond streamed results | Baseline |
| C1 | Bash rewritten to run in a Cleat session | `PreToolUse` → `updatedInput.command` (wrapper that launches or sends into a session, waits, prints output, exits with its code) | Tool name, schema, output limits, spill, task machinery | `run_in_background` backgrounds the wrapper, not the session's process; output timing depends on the wrapper's wait condition; hooks race | Exit-code and output fidelity; latency; whether humans watch |
| C2 | Cleat terminal tools beside Bash | `createSdkMcpServer` (`launch`, `send`, `wait`, `capture`) | All stock tools | The model must choose; the cleat-sessions skill already gives this through Bash | How often the model reaches for TUI tools; success on interactive tasks |
| C3 | Bash replaced by a hosted shell | `tools`/`disallowedTools` + MCP tool + `toolAliases { Bash: … }` | Model's habit of calling "Bash" (via alias) | Background tasks, auto-backgrounding, Bash-as-read, stale-read hints, sandbox | Trained-tools hypothesis: task success and error rate vs C0 |
| C4 | File tools reaching a remote workspace | MCP `Read`/`Edit`/`Write` replacements; or keep built-in Edit and `seedReadState(path, mtime)` | Edit semantics if seeded | Checkpoints, `CLAUDE.md`-on-view, read check unless rebuilt | Edit failure rate; drift between remote and cache |
| C5 | Whole CLI where the workspace is | `spawnClaudeCodeProcess` | Every stock tool and behaviour | Commands still not in a Cleat session; process placement and auth | Same tasks, local vs remote |

### Codex app-server

| # | Addition | API | Keeps | Gives up / risks | Measure |
| --- | --- | --- | --- | --- | --- |
| X0 | Stock, observed | `outputDelta`, `terminalInteraction`, `backgroundTerminals/list` | Everything | — | Baseline; already enough to mirror sessions into views |
| X1 | Host-run environment backed by Cleat | `environment/add` + exec-server protocol (`process/start { tty }`, `process/write`, `fs/*`) | Model's tools unchanged (`exec_command`, `write_stdin`, `apply_patch`) | Experimental API; we implement a server; remote envs skip zsh-fork | Parity with local env; whether each `process/*` maps onto a Cleat session |
| X2 | Cleat tools beside the shell | `thread/start.dynamicTools`, `item/tool/call` | All built-ins | Cannot shadow `exec_command`; experimental | Use rate; overlap with `tty: true` |
| X3 | Shell off, hosted shell as a dynamic tool | `features.shell_tool=false` + a differently named dynamic tool | apply_patch | Polling sessions, PTY, approvals; model loses its trained tool | Trained-tools hypothesis vs X0 |
| X4 | Command rewrite | Config `PreToolUse` hook `updated_input` | Tool and protocol | Config-level, not per-client | As C1 |

### pi and pi durable

| # | Addition | API | Keeps | Gives up / risks | Measure |
| --- | --- | --- | --- | --- | --- |
| P1 | Cleat-backed bash and file operations | `BashOperations`, `Read/Write/EditOperations` via extension or SDK | Tool schemas and rendering | Grep needs its own override | Parity with local |
| P2 | Input rewrite | `tool_call` event mutating `event.input` | Everything else | No re-validation after mutation | As C1 |
| P3 | Replacement tool | `pi.registerTool()` same name | Name | Must match result shape | As C3 |
| P4 | Durable env on a Cleat or remote host | pi durable `env` → `ExecutionEnv` (`Shell.exec`, `FileSystem`); `pi-env` as a pattern | Crash-durable tool calls; `pi.live` watch stream | Experimental API | Recovery after a host restart |

### Vercel AI SDK (our own loop, never required)

| # | Addition | API | Measure |
| --- | --- | --- | --- |
| V1 | Our tools on a Cleat-backed sandbox | `Experimental_SandboxSession` (`spawn` with `stdin`), `@ai-sdk/tool-bash` | Interactive tasks with a real stdin |
| V2 | Vendor tool schemas, our execution | `anthropic.bash_20250124`, `text_editor_20250728`, `openai.shell`, `openai.apply_patch` | Trained-tools hypothesis with execution held constant |
| V3 | Wrapped harnesses on our sandbox | `@ai-sdk/harness` adapters | Whether a harness adapter can stand in for our producers |

### ACP

| # | Addition | API | Measure |
| --- | --- | --- | --- |
| A1 | Wheelhouse as a v1 client offering `fs` and `terminal` | `fs/*`, `terminal/*` backed by Cleat | Only Gemini CLI uses fs today; adapters for Claude and Codex no longer call these; v2 drops them |
| A2 | Render display-only terminals | `_meta.terminal_output`, v2 `terminal_update`/`terminal_output_chunk` | A shape for the Agent View's terminal cards, whatever harness produces them |

## Open questions

1. **Trained-tools hypothesis.** Do task success, tool-error rate and retries
   change when a stock tool is replaced (C3, X3, V2) rather than rewritten
   (C1, X4) or hosted underneath (X1, P1, C5)? Needs a small fixed task set
   run across rows.
2. **Command boundaries in Cleat.** How does a wrapper learn a command's exit
   status and output span in a session: OSC 133 marks (seen by Cleat's VT),
   `mark`/`transcript`, or a new Cleat verb? Is a new verb wanted?
3. **One session per command or one long shell?** Per-command sessions match
   every harness's semantics; a long shell would carry `cd`/`export` that the
   harnesses say do not carry. Which do people want to watch?
4. **PTY vs pipes.** Codex defaults to pipes with `TERM=dumb`; Cleat is a
   PTY. Do tools that colour or page under a PTY confuse the model's output
   parsing? Is Claude's Bash a PTY **[unverified]**?
5. **Claude background tasks under C1.** Can a rewritten `run_in_background`
   command keep its `task_notification` while the real process lives in
   Cleat (the wrapper waits for the session), and what does `stopTask` then
   stop?
6. **Claude checkpoints and MCP edits.** Does `rewindFiles` restore edits made
   by an MCP replacement or by Bash **[unverified]**?
7. **Claude `defer`.** Can a host defer a tool call, run it itself, and feed
   the result back on resume **[unverified]**?
8. **Codex `config` and features.** Does `thread/start.config` accept
   `features.shell_tool=false` per thread **[unverified]**? Do shell-made
   changes reach `turn/diff/updated` **[unverified]**?
9. **Codex exec-server stability.** `environment/add` is experimental; which
   release makes it stable, and is a non-Codex exec-server supported or only
   tolerated?
10. **Where Cleat sessions live.** Cleat sessions can be shared and remote;
    when the loop and the workspace are on different machines, which side
    owns the session, and how does a person find it from the Agent View?
11. **ACP v2's direction.** With client terminals removed, display-only
    terminals are what editors will get. Does the Agent View want the same
    split: harness owns execution, Wheelhouse displays it, Cleat optional?
