# Agent composers: slash commands and completions through the harness (researched 2026-11-05)

Scope: where agent apps get the slash commands, skills and `@` completion candidates their
composers offer; what a chosen command turns into; how `@` finds and inserts candidates; and
which commands a remote or second client can't run. It answers
[#689](https://github.com/flotilla-org/wheelhouse/issues/689) for
[#694](https://github.com/flotilla-org/wheelhouse/issues/694), part of the
[Agent View map](https://github.com/flotilla-org/wheelhouse/issues/686). The key concern is
that the composer must not go behind the agent harness: commands and completions should come
through what the harness offers.

The [composer survey](https://github.com/flotilla-org/wheelhouse/blob/research/agent-composers/docs/research/agent-composers.md)
already covers keys, layout, busy states and how mentions look (chips or plain text). This
note does not repeat it.

**Words used here.** The *harness* is the process that owns the agent loop: the Claude Code
CLI (also what the Claude Agent SDK runs), Codex core behind `codex app-server`, the opencode
server, or an ACP agent. A *client* is anything that drives it: a TUI, an IDE panel, a web app,
or the Wheelhouse Agent View.

**Sources.** I read code at these commits (fetched 2026-11-05). Paths are relative to each repo.

| Project | Repo | Commit |
|---|---|---|
| Codex (TUI, app-server, core) | `openai/codex` | `75aba6768a32622f145f97c6ce6c46fd92f4271a` (same as the composer survey) |
| T3 Code | `pingdotgg/t3code` | `d51bc58f5e05123a416a3737ac4bce42d1d76b7c` (same) |
| opencode | `anomalyco/opencode` | `fe046564650854594c4c5aa22270f46d9de6cace` (same) |
| Zed | `zed-industries/zed` | `dd3437011469d5c2ec8c821a84cc999979625697` (same) |
| Agent Client Protocol | `agentclientprotocol/agent-client-protocol` | `734309dc6ff8f5a3b93adca62826dd63cff85a26` |
| Claude ACP adapter (`@agentclientprotocol/claude-agent-acp` 0.100.0) | `zed-industries/claude-code-acp` | `7ea9eec8392a43191b25a5c3636c181cc7eb42f3` |
| Codex ACP adapter | `zed-industries/codex-acp` | `296069e841634cd4bb9bc4515602d836e49231ec` |
| Claude Agent SDK (TypeScript) | npm `@anthropic-ai/claude-agent-sdk` | 0.3.318 (`claudeCodeVersion` 2.1.318), file `sdk.d.ts` |

For closed products I used official docs, linked in each section: Claude Code
(code.claude.com), the Codex app (learn.chatgpt.com), and Cursor (cursor.com/docs).
**[unconfirmed]** marks a point I couldn't find in a primary source.

---

## Comparison

| App | Where the `/` list comes from | What a chosen command becomes | Where `@` candidates come from, and what goes in | What a second client can't run |
|---|---|---|---|---|
| **Claude Code TUI** | The CLI is the harness: built-ins, bundled skills, your skills, `.claude/commands/`, plugins, MCP prompts. | The CLI runs it. | The CLI's own file index (files, directories, MCP resources, subagents). Inserts `@path` text; the CLI attaches the content on send. | n/a: it is the terminal. |
| **Claude Agent SDK** (any client) | **Advertised.** `system/init` has `slash_commands`, `skills`, `agents`; `supportedCommands()` gives name, description, argument hint, aliases, `builtin`; `commands_changed` pushes a replacement list. | **Text.** Send `/name args` as the prompt; the CLI dispatches it. | The CLI expands `@path` in the prompt text. A `file_suggestions` control request exists in the wire types, but the SDK exposes no method for it. | **Advertised.** Interactive-only commands are left out; `terminal_slash_commands` names the ones bound to a terminal so remote UIs hide them. |
| **Claude Code Remote Control** (claude.ai/code, mobile) | The local session's list. | Text to the local CLI. | "Typing `@` autocompletes file paths from your local project." | Documented: `/plugin`, `/resume` and similar are local-only; `/model`, `/effort` take an argument instead of a picker. |
| **Codex TUI** | **Client table** of built-ins, plus service-tier commands from the model catalog. Skills, plugins and apps for the `$`/`@` popup come from app-server `skills/list`, `plugin/list` and similar. | Mostly **a separate app-server request** (`/compact` → `thread/compact/start`, `/review` → `review/start`, `/goal` → `thread/goal/*`). Some are canned prompts (`/init`) or run in the client (`/theme`). | Files from an in-process `codex-file-search` session over the cwd. A file goes in as **plain path text**. Skills and plugins go in as `$name` with a binding sent as `UserInput::Skill`/`Mention`. | Gated in the client: commands not allowed during a turn, in a side conversation, or with a remote workspace (local worktree operations). |
| **Codex app-server** (any client) | **No command list.** It advertises skills (`skills/list`, `skills/changed`), plugins, apps, models, collaboration modes. | Each operation is its own method. Skills can be plain text: core parses `$skill-name` and `[$name](path)` in text input. | `fuzzyFileSearch` (one-shot) and `fuzzyFileSearch/session*` (streaming, experimental) over roots the client passes. | Not modelled: a client can't run a "command" the server doesn't have, because there are none. |
| **Codex app** (closed) | "Enabled skills also appear in the slash command list"; custom prompts as `/prompts:<name>`. "Available commands vary based on your environment and access." | **[unconfirmed]** (likely app-server requests, as in the TUI). | `$` for skills; `plugin://` mentions. File `@` **[unconfirmed]**. | `/cloud`, `/approve`, `/memories` appear only when available. |
| **ACP** (Zed and others) | **Advertised.** `available_commands_update` (full replacement list); in v2 draft also in `session/new` and `session/resume` responses. | **Text.** "Commands are included as regular user messages in prompt requests." | **The client** searches; ACP has no completion request. A mention goes in as a `resource_link`, or an embedded `resource` when the agent advertises `embeddedContext`. | Not in the protocol. Adapters filter what they advertise. |
| **Zed agent panel** | The thread's ACP commands. Skills only for Zed's own agent. | Text in the prompt. Zed's own built-ins (`/compact`) run as a bare command turn and queue the rest of the line. `/login` and `/logout` are handled by Zed. | Zed's project index: files and symbols always; threads, diagnostics, fetch, skills and branch diff only with `embeddedContext`. Inserted as a crease that becomes a resource block. | Whatever the adapter drops (see claude-agent-acp, codex-acp). |
| **T3 Code** | **Mixed.** Claude: commands from a capabilities probe (`initializationResult().commands`); skills read from Claude's config directories by T3 itself. Codex: skills from `skills/list`; commands are a fixed T3 list (`compact`, `goal`, `feedback`). ACP: `available_commands_update`. T3 adds its own (`/model`, `/plan`, `/side`). | Provider commands are inserted as `/name ` text. On send T3 turns `/compact` into the provider's compaction call and `/goal` into `thread/goal/*` for Codex; other text goes as a prompt. Skills go in as `$name`. | **T3's own server** searches the project (`searchEntries`), not the harness. A file goes in as a Markdown link `[name](path)`. | No terminal filter: I found no production use of `terminal_slash_commands` or `commands_changed`. |
| **opencode** | **Advertised by the server**: `GET /api/command` (`name`, `description`), invalidated by a `command.updated` event. The TUI adds its own keymap commands. | **A separate request**: `POST /api/session/:id/command` with `name` and the prompt input. | Server: `GET /api/fs/find`, `/api/agent`, `/api/skill`, `/api/reference`. Inserted as typed **parts** (file, agent, skill) beside the text. | Local file paths are sent as `file://` URIs only when the server is local. |
| **Cursor** (closed) | Built-ins plus skills from `.cursor/skills`, `.agents/skills`, and for compatibility `.claude/skills` and `.codex/skills`. | "Agent reads the skill file and follows the instructions." | Files and folders, terminals, chats, git diffs, browser. | Over ACP: "Team-level MCP servers … are not supported." Commands over ACP **[unconfirmed]**. |

---

## Claude Code and the Claude Agent SDK

Sources: [Commands](https://code.claude.com/docs/en/commands),
[SDK commands and skills](https://code.claude.com/docs/en/agent-sdk/slash-commands) (that URL serves
the "Extend agents with skills" page, section "Commands in Agent SDK sessions"),
[TypeScript SDK reference](https://code.claude.com/docs/en/agent-sdk/typescript),
[Common workflows: reference files](https://code.claude.com/docs/en/common-workflows),
[Subagents](https://code.claude.com/docs/en/sub-agents),
[Remote Control](https://code.claude.com/docs/en/remote-control), and `sdk.d.ts` from
`@anthropic-ai/claude-agent-sdk` 0.3.318.

**Where the list comes from.** The CLI is the harness, and it owns one command surface. The docs
list four kinds behind it: built-in commands ("execute logic coded into the Claude Code
process"), bundled skills, your skills (`SKILL.md`), and custom command files in
`.claude/commands/`. MCP servers can also expose prompts as commands. The SDK hands the list to
a client in three ways:

- The `system/init` message has `slash_commands: string[]`, `skills: string[]`,
  `agents?: string[]` and `plugins` (`SDKSystemMessage`, `sdk.d.ts:6309`). The docs: "The
  printed list mixes built-in commands, bundled skills, your user-invocable skills, and
  `.claude/commands/` files." It omits skills marked `user-invocable: false`.
- `Query.supportedCommands(): Promise<SlashCommand[]>` and `initializationResult().commands`
  give the rich form (`sdk.d.ts:9876`): `name`, `description`, `argumentHint`, `aliases?`, and
  `builtin?`, which is "True when the command is Claude Code's own; absent for a command defined
  by a user, project, plugin or MCP server". `supportedAgents()` returns `AgentInfo` (`name`,
  `description`, `model?`).
- `SDKCommandsChangedMessage` (`subtype: 'commands_changed'`, `sdk.d.ts:3877`) is a
  "Fire-and-forget push of the full slash-command list after a mid-session change (e.g. skills
  discovered dynamically as the agent works in a subdirectory). Clients should REPLACE their
  cached command list with this payload". From SDK 0.3.216, `supportedCommands()` follows it.

**What a chosen command becomes.** Text. "Send a command by including it in your prompt string,
the same way you send regular text." A command is recognized only at the start of a message,
and the rest of the line is its argument. Commands sent mid-turn are queued until the turn ends,
except a few that run at once, such as `/status` and `/usage` ([Commands](https://code.claude.com/docs/en/commands)).
Unknown or unavailable names don't fail:

- "A `/<name>` that matches neither a command in the session nor a built-in Claude Code command
  doesn't fail the query. Claude Code sends the prompt to Claude as an ordinary message, with a
  note that the command didn't run" (since 2.1.274).
- "A `/<name>` that matches a built-in Claude Code command that isn't available in the session,
  such as `/theme`, returns `/theme isn't available in this environment.` as the result without a
  model turn."
- Text-only output from a local command arrives as `SDKLocalCommandOutputMessage`
  (`subtype: 'local_command_output'`), "Displayed as assistant-style text in the transcript".

One SDK option matters for a composer: `client_composed: true` delivers the text "as written: no
`@path` file-mention expansion and no slash-command dispatch" (`sdk.d.ts:1949`). It is meant for
text the user did not type. A composer sending what the user typed should leave it off.

**`@` completions.** The CLI does the work on text:

- "Type `@` to open a path suggestion menu". `@src/utils/auth.js` "includes the content of the
  file in the conversation when it fits the Read tool's token limit"; a directory reference
  shows a listing; `@server:resource` fetches an MCP resource
  ([common workflows](https://code.claude.com/docs/en/common-workflows)).
- `@agent-<name>` (or the picker's `@"code-reviewer (agent)"`) makes Claude invoke that
  subagent. "The @-mention controls which subagent Claude invokes, not what prompt it receives"
  ([subagents](https://code.claude.com/docs/en/sub-agents)).
- So the inserted thing is plain text, and the harness expands it on send.
- Candidates: the stream-json control protocol declares a `file_suggestions` request
  ("Requests at-mention file autocomplete suggestions for a partial path prefix. Returns the
  same fuzzy-matched results the TUI shows", `sdk.d.ts:4071`). It is not exported, `sdk.mjs`
  never sends it, and its response type isn't declared. Treat it as internal
  **[unconfirmed whether it is meant for third-party clients]**. A `fileSuggestion` setting lets
  a user plug in a custom command for `@` suggestions (`sdk.d.ts:7169`).
- Remote Control says "typing `@` autocompletes file paths from your local project", so
  Anthropic's own remote clients get candidates from the local CLI **[unconfirmed which
  request they use]**.

**What a second client can't run.**

- The SDK list already leaves out commands "that need an interactive terminal, such as `/theme`
  and `/terminal-setup`".
- `terminal_slash_commands?: string[]` in `system/init` is the "Subset of slash_commands whose
  UX is bound to the local terminal (e.g. exit, statusline). Phone/remote UIs should hide these
  from command menus; desktop surfaces may keep them" (`sdk.d.ts:6337`).
- Remote Control documents the rest: "commands that only run in the terminal interface, such as
  `/plugin` or `/resume`, work only from the local CLI". Text-output commands (`/compact`,
  `/clear`, `/context`, `/usage` …) work. `/model`, `/effort`, `/fast`, `/color` and `/rename`
  take the value as an argument instead of a picker. `/mcp` returns a text summary on mobile.
- The command menu also hides commands that aren't available to the user, and a few by design
  (`/heapdump`) until the full name is typed.

### Claude through ACP (`claude-agent-acp`)

The adapter shows how a second client keeps the harness in charge (`src/acp-agent.ts`):

- It builds `available_commands_update` from `query.supportedCommands()` (line 3966) and
  re-sends it on `commands_changed` (around line 6591).
- It drops every name in `terminal_slash_commands` (field comment at line 1158: "ACP clients
  aren't that terminal, so these are filtered out"), plus a fixed `UNSUPPORTED_COMMANDS` list
  (`clear`, `cost`, `keybindings-help`, `login`, `logout`, `output-style:new`,
  `release-notes`, `todos`, line 13785).
- It maps `argumentHint` to ACP `input.hint`, renames MCP prompts to `mcp:server:prompt`, and
  marks skills in `_meta` with their `SKILL.md` path.
- It runs `/mcp` and `/remote-control` itself, the only commands it doesn't pass through.
- In a prompt, a `resource_link` becomes the text `[@name](file:///…)`, and an embedded
  `resource` becomes that link plus a `<context ref="…">` block (`promptToClaude`, around line
  13893).

---

## OpenAI Codex

### TUI (`codex-rs/tui/src/`)

**Where the list comes from.**

- `/` is a **client table**. `slash_command.rs` defines `enum SlashCommand` ("Enum order is
  presentation order in the popup"), about 70 entries with descriptions and flags:
  `available_during_task`, `available_in_side_conversation`,
  `available_when_thread_unavailable` and `supports_inline_args`.
- `bottom_pane/slash_commands.rs` filters it by feature flags (`builtins_for_input`). It adds
  `ServiceTier` commands built from the model catalog, which the server supplies.
- The `$`/`@` popup (`bottom_pane/mentions_v2/`, `search_catalog.rs`) mixes skills, plugins,
  tasks, files and directories. Skills come from app-server `skills/list`
  (`app_server_session.rs:1993`). Tasks come from `thread/list` and `thread/search`
  (`task_mentions.rs`).

**What a chosen command becomes** (`chatwidget/slash_dispatch.rs`).

- **App-server requests**, for example `/compact` → `app_event_tx.compact()` → `thread/compact/start` (line 335),
  `/review` → a picker then `review/start`, `/goal` → `thread/goal/*`, `/rename` → `thread/name/set`.
- **A canned prompt:** `/init` submits `prompt_for_init_command.md` as a user message (line 331).
- **Client-run, on the server host:** `/diff` runs `git diff` through app-server `command/exec`.
  `workspace_command.rs` says this "lets status surfaces avoid knowing whether the current
  app-server is embedded or remote" (line 501 of `slash_dispatch.rs`).
- **Client-only UI:** `/theme`, `/keymap`, `/vim`, `/statusline`, `/copy`.
- An unknown `/name` is **not sent**. The composer shows "Unrecognized command '/{name}'. Type
  "/" for a list of supported commands." and restores the draft (`bottom_pane/chat_composer.rs:3127`).

**`@` completions.**

- Files: `file_search.rs` runs an in-process `codex-file-search` session over `config.cwd`,
  updated on every keystroke of the `@token`.
- With mentions v2, task candidates also come from the app-server (`app/event_dispatch.rs:1923`).
- A chosen file goes in as **plain path text** (`insert_selected_file_path`,
  `chat_composer.rs:2458`). Skills, plugins and apps go in as `$name` with a binding. On submit
  the binding becomes a structured `UserInput::Skill { name, path }` or
  `UserInput::Mention { name, path }`.
- I didn't find the TUI switching to app-server `fuzzyFileSearch` for a remote workspace
  **[unconfirmed how `@` file search behaves with a remote app-server]**.

**What a second client can't run.** The TUI gates commands itself:

- "'/{cmd}' is disabled while a task is in progress."
- "'/{cmd}' is unavailable in side conversations."
- Local worktree operations are off when the workspace is remote (`app/startup.rs:934`).
- Debug-only commands are hidden in release builds.

### app-server (the harness interface)

`app-server-protocol/src/protocol/common.rs` at the pinned commit:

- **No slash-command list.** There is no `commands/list`. Operations a TUI command performs
  are separate methods: `thread/compact/start`, `review/start`, `thread/goal/set`,
  `thread/name/set`, `thread/fork`, `thread/shellCommand`, `model/list`,
  `collaborationMode/list` and others.
- **Skills:** `skills/list` (line 923) takes `cwds?` and `forceReload?` and returns
  `SkillsListEntry { cwd, skills: SkillMetadata[], errors }`. A `SkillMetadata` has `name`,
  `description`, `shortDescription?`, `interface?`, `path`, `scope`
  (`user | repo | system | admin`), `enabled` and `pluginId`.
- **`skills/changed`** (line 2028) is an invalidation signal: "re-run `skills/list` with the
  client's current parameters". Also `plugin/list`, `app/list` and `hooks/list`.
- **Files:** `fuzzyFileSearch` (line 1568) takes `{ query, roots, cancellationToken }` and returns
  `FuzzyFileSearchResult { root, path, match_type, file_name, score, indices }`. The experimental
  `fuzzyFileSearch/sessionStart|Update|Stop` streams results through `sessionUpdated` and
  `sessionCompleted` notifications. The client supplies the roots.
- **How skills and mentions are sent.** `UserInput` has `Text { text, text_elements }`, `Skill
  { name, path }` and `Mention { name, path }` (`v2/turn.rs:474`). Core doesn't need the
  structured form: `collect_explicit_skill_mentions` resolves `UserInput::Skill` first, "Text
  inputs are then scanned to extract `$skill-name` tokens", and plain names are used "only when
  the match is unambiguous" (`skills/src/selection.rs:44`). `extract_tool_mentions` also accepts
  "explicit resource links in the form `[$tool-name](resource path)`" with `skill://`,
  `plugin://`, `app://` and `mcp://` prefixes (`skills/src/mentions.rs:84`).
- **There is no `@file` expansion in core.** A file path in text is just text for the model to
  read with its tools.

### Codex app and IDE extension (closed)

[Slash commands](https://learn.chatgpt.com/codex/reference/slash-commands):

- "Available commands vary based on your environment and access."
- "Enabled skills also appear in the slash command list", and "You can also explicitly invoke
  skills by typing `$` in the chat composer". Custom prompts show as `/prompts:<name>`.
- 28 commands are listed (approve, archive, chat, cloud, compact, fast, goal, init, model,
  review, side, worktree and others).
- `/cloud` appears when "cloud execution is available", `/approve` "when automatic review is
  active", and `/memories` "when Memories is available".
- File `@` and how commands reach the server are **[unconfirmed]**.

### Codex through ACP (`codex-acp`)

`src/thread.rs`:

- The adapter advertises a fixed list 200 ms after load: `review`, `review-branch`,
  `review-commit`, `init`, `compact` and `logout` (`builtin_commands`, line 2903).
- On prompt it recognises those names and maps them to Codex operations, such as `"compact" =>
  Op::Compact` (line 3183).
- It advertises no skills at this commit. A `$skill` typed as text would still reach Codex core,
  which parses it.

---

## Agent Client Protocol

Sources: `docs/protocol/v1/slash-commands.mdx`, `docs/protocol/v2/draft/slash-commands.mdx`,
`docs/protocol/v1/content.mdx`, `docs/protocol/v1/initialization.mdx` at the pinned commit
([published docs](https://agentclientprotocol.com/protocol/slash-commands)).

**Where the list comes from.** The agent advertises it:

- "Agents can advertise a set of slash commands that users can invoke … After creating a
  session, the Agent **MAY** send a list of available commands via the
  `available_commands_update` session notification."
- An `AvailableCommand` has `name`, `description` and an optional `input: { hint }`.
- "The Agent can update the list of available commands at any time during a session."
- The v2 draft makes it stricter. An agent with commands "**MUST** include them in
  `availableCommands` in the `session/new`, `session/resume`, and, when supported,
  `session/fork` responses". The notification "is the complete replacement list, not a delta".
- In the v2 draft each entry has a `type` (only `command` today; custom types start with `_`).
  Names "**MUST NOT** contain whitespace and **MUST** be unique".

**What a chosen command becomes.** Text: "Commands are included as regular user messages in
prompt requests … The Agent recognizes the command prefix and processes it accordingly. Commands
may be accompanied by any other user message content types (images, audio, etc.) in the same
prompt array."

**`@` completions.** ACP has no completion or search request, so the client finds candidates
itself. It carries the choice as content:

- "As a baseline, all Agents **MUST** support `text` and `resource_link` content blocks."
- An embedded `resource` needs the `embeddedContext` prompt capability, and "This is the
  preferred way to include context in prompts, such as when using @-mentions to reference files
  or other resources. By embedding the content directly in the request, Clients can include
  context from sources that the Agent may not have direct access to."

**What a second client can't run.** The protocol has no field for it. The agent is expected to
advertise only what an ACP client can use, and adapters do the filtering (see
claude-agent-acp and codex-acp above).

---

## Zed agent panel (`crates/agent_ui`, `crates/acp_thread`)

**Where the list comes from.**

- Every agent, including Zed's own, reaches the panel over ACP. The panel keeps the thread's
  `available_commands` and updates them from `available_commands_update`
  (`agent_servers/src/acp/session_updates.rs:344`).
- Zed's own agent advertises `/compact` and its MCP server prompts. It tags them with
  `_meta.command_category` set to `native` or `mcp`. The doc comment on that key (`acp_thread.rs:120`):
  "Only the native Zed agent annotates its commands; commands from external ACP agents carry no
  category".
- Skills appear in the `/` and `@` popups only for Zed's own agent
  (`native_available_skills`, `conversation_view.rs:1571`). External agents' skills appear only
  if the agent lists them as commands, as claude-agent-acp does.

**What a chosen command becomes.**

- Completion inserts `/name ` text, and the prompt goes as a text block.
- Zed treats two cases specially (`conversation_view/thread_view.rs:2220`):
  - `/login` and `/logout` open Zed's own auth flow when the agent has auth methods and doesn't
    advertise `logout` itself.
  - A *native* command (category `native`) is sent bare, without echoing it as a user message,
    and "everything the user typed after it" is queued as a follow-up.
    `leading_native_command` excludes "Commands from MCP servers and ACP agents … their
    trailing text is a real argument the agent consumes" (line 14500).

**`@` completions.**

- Zed searches its own project: file paths through `fuzzy::match_path_sets` (`search_files`,
  `completion_provider.rs:2243`), symbols through the language servers, plus threads,
  diagnostics, fetch, skills and branch diff (`PromptContextType`, line 158).
- Which kinds are offered depends on the agent. `supported_modes` offers only File and Symbol
  unless the agent has `embeddedContext` (`message_editor.rs:99`).
- On send each crease becomes an embedded `resource` with the text when `embeddedContext` is
  supported, otherwise a `resource_link` (`mention_to_content_block`, `message_editor.rs:2202`).

**What a second client can't run.** Whatever the adapter didn't advertise. Zed adds nothing of
its own beyond the `/login`/`/logout` takeover.

---

## T3 Code

T3 Code runs a server that drives the harnesses (Claude Agent SDK, Codex app-server, ACP
agents, opencode and others). Its web and desktop apps are clients of that server.

**Where the list comes from.** It varies by provider:

- **Claude.** `probeClaudeCapabilities` starts a short probe query and reads
  `initializationResult()`. `commands` → `slashCommands` (`ClaudeProvider.ts:415`) and `models`
  are kept. `/compact` is always added.
  - Skills don't come from the SDK. `discoverClaudeSkills` scans `<claude config>/skills`,
    `<cwd>/.claude/skills`, `--add-dir` directories and enabled plugin skill roots itself
    (`Drivers/ClaudeSkills.ts:496`). That reimplements the harness's discovery rules from its
    config files.
  - The list is per workspace snapshot from the probe, not from the live session. I found no
    production use of `commands_changed` or `terminal_slash_commands`.
- **Codex.** Skills come from `skills/list` with the thread cwd (`CodexProvider.ts:556`).
  Commands are a fixed T3 list: `compact`, `goal` and `feedback` (`CodexProvider.ts:872`).
- **ACP providers.** `available_commands_update` (`packages/provider-acp/src/server/adapter.ts:6201`).
- **T3's own entries.** `/model`, `/plan`, `/default`, `/side` and `/btw` act on T3 state
  (`useComposerMenuItems.ts:218`).

**What a chosen command becomes.**

- A provider command is inserted as `/name ` text (`ChatComposer.tsx:1904`), and the message
  goes to the server as text.
- On the server, a message that is exactly `/compact` with no attachments calls the provider's
  `compactThread` instead of starting a turn (`RunExecutionService.ts:1728`). `/compact` and
  `/logout` are "native maintenance commands" (`CommandPolicy.ts:68`).
- For Codex, `/goal` becomes `thread/goal/*` and "never reaches Codex" as text
  (`CodexAdapterV2.ts:909`).
- A skill is inserted as `$name ` (`ChatComposer.tsx:1930`). For Codex, other sigils are
  normalised because "Codex only parses `$name` as a skill mention" (`CodexAdapterV2.ts:434`).

**`@` completions.**

- T3's own server searches the project (`projectEnvironment.searchEntries`,
  `apps/web/src/state/queries.ts:324`). It doesn't ask Codex `fuzzyFileSearch` or Claude.
- A chosen path is serialised as a Markdown link `[basename](path)` (`serializeComposerFileLink`,
  `packages/shared/src/composerTrigger.ts:34`). `#` inserts a PR reference.

**What a second client can't run.** Not modelled per command. `/compact` is hidden when
`compactSlashCommandAvailable` is false (`useComposerMenuItems.ts:292`), and an unsupported
provider gets "This provider does not support context compaction."

---

## opencode (`packages/protocol`, `packages/tui`, `packages/app`)

The pinned commit has opencode split into a server with an HTTP API (`packages/protocol`) and
TUI, web and desktop clients.

**Where the list comes from.**

- The server advertises it. `GET /api/command` ("Retrieve currently registered commands.",
  `protocol/src/groups/command.ts`) returns `Command.Info { name, description? }`.
- The schema declares a `command.updated` event (`schema/src/command.ts`).
- Commands are registered server-side by plugins and config (`core/src/command.ts`, `Definition
  { name, description?, execute }`).
- The TUI merges these with its own keymap commands that have a `slash` binding
  (`tui/src/component/prompt/autocomplete.tsx:490`). Server commands are marked `queueable`.

**What a chosen command becomes.** A separate request.

- Choosing a command inserts `/name ` text.
- On submit, if the leading word matches a server command, the TUI calls
  `POST /api/session/:sessionID/command` with `name`, the arguments as `text`, and any `files`,
  `agents` and `skills` parts (`tui/src/component/prompt/index.tsx:1155`, `1320`). The endpoint
  is described as "Execute a slash command callback immediately."
- Skills also have `POST /api/experimental/session/:id/skill` ("Activate a skill for a session
  by appending a skill message and resuming execution").
- A keymap command runs in the client.

**`@` completions.**

- The server answers: `GET /api/fs/find` ("Find recursively ranked filesystem entries relative
  to the requested location", `protocol/src/groups/fs.ts:69`), `GET /api/agent`,
  `GET /api/skill` and `GET /api/reference`.
- The TUI asks `client.api.file.find({ query, limit: 20, location })` (`autocomplete.tsx:378`).
- A choice is inserted as a styled span backed by a typed **part** (`type: "file" | "agent" |
  "skill"`), sent beside the text (`autocomplete.tsx:424-480`).

**What a second client can't run.** Clients can attach to a remote server. Files picked on the
client are sent as `file://` URIs only "when the server is local"
(`promptFilesForServer`, `tui/src/component/prompt/local-attachment.ts:64`). I found no
per-command remote flag.

---

## Cursor (closed)

Sources: [Skills](https://cursor.com/docs/skills),
[CLI slash commands](https://cursor.com/docs/cli/reference/slash-commands),
[Context](https://cursor.com/help/customization/context),
[CLI ACP](https://cursor.com/docs/cli/acp).

- **Where the list comes from.** Built-ins (the CLI lists `/model`, `/plan`, `/ask`, `/goal`,
  `/summarize`, `/rewind`, `/mcp`, `/plugin` and others) plus skills.
  - "Skills load automatically from project-level `.agents/skills/` and `.cursor/skills/`, and
    from user-level `~/.agents/skills/` and `~/.cursor/skills/`. For compatibility, Cursor also
    loads skills from Claude and Codex directories". That is Cursor reading other harnesses'
    config files.
  - Commands were folded into skills: `/migrate-to-skills` converts "Both user-level and
    workspace-level commands … preserving their explicit invocation behavior."
- **What a chosen command becomes.** "Skills can also be manually invoked by typing `/` in Agent
  chat and searching for the skill name. A skill invoked this way attaches to one message."
  Cursor is both client and harness, so there is no boundary to cross.
- **`@`.** "@auth.ts" or "@src/components/" for files or folders, "@Terminals", "@Chats",
  "@Commit (Diff of Working State)", "@Branch (Diff with Main)", "@Browser". Each "gets added to
  the conversation."
- **Second client.** The ACP page says only that "Team-level MCP servers configured through the
  Cursor dashboard are not supported in ACP mode." Whether `cursor-agent acp` sends
  `available_commands_update` is **[unconfirmed]**.

---

## Synthesis

### The patterns

1. **The harness advertises a list, and the command travels as text.** Claude (`system/init`
   `slash_commands`, `supportedCommands()`, `commands_changed`) and ACP
   (`available_commands_update`) work this way. The client shows the list, inserts `/name args`,
   and sends an ordinary prompt; the harness parses the leading word. Arguments are free text with
   a hint. Unknown names degrade safely: Claude sends them to the model with a note, ACP leaves it
   to the agent.
2. **The harness advertises a list, and the command is a separate request.** opencode works this
   way (`GET /api/command` and `POST /api/session/:id/command`). The client still offers it as
   `/name` text and decides on submit whether the leading word is a server command.
3. **The harness exposes operations, and the client owns the command table.** Codex app-server
   has no command list. The TUI, T3 and codex-acp each keep their own table and map entries to
   methods (`thread/compact/start`, `review/start`, `thread/goal/*`) or canned prompts (`/init`).
   Skills are the exception: Codex advertises them (`skills/list`, `skills/changed`) and parses
   `$name` in plain text.
4. **The client reads the harness's files.** T3 for Claude skills, and Cursor reading
   `.claude/skills`. This works until the harness's discovery rules change, and it goes behind
   the harness.
5. **`@` candidates come from wherever the files are.** The harness has a search in
   Claude (internal `file_suggestions`, used by its own remote clients), Codex
   (`fuzzyFileSearch`, client-supplied roots) and opencode (`fs.find`, plus agents, skills and
   references). Zed, T3 and the Codex TUI search on the client side and assume the client sees
   the same files as the agent. ACP has no search, so the client must search.
6. **What goes in for `@`.** Claude: plain `@path` text, which the CLI expands (file content,
   CLAUDE.md files, MCP resources, `@agent-` subagents). Codex: plain path text, and `$name` or
   `[$name](skill://…)` for skills, which core resolves. ACP: `resource_link`, or an embedded
   `resource` if the agent allows it. opencode: typed parts. T3: a Markdown link.
7. **Commands a second client can't run.** Claude marks them in the protocol
   (`terminal_slash_commands`, and the SDK list leaves out interactive-only commands). ACP
   adapters filter before advertising. The Codex TUI gates in the client (busy, side
   conversation, remote workspace). The others don't model it.

### Which keep the harness in charge

- **Claude Agent SDK.** The strongest: the list, live updates, terminal-bound marks, dispatch,
  `@` expansion and the unknown-command fallback all belong to the CLI. A client that shows
  `supportedCommands()` minus `terminal_slash_commands`, follows `commands_changed`, and sends
  text never goes around it.
- **ACP.** In charge of the list and of interpretation. File candidates are the client's job by
  design, and the client can embed content the agent can't reach.
- **opencode.** In charge of the list, execution and file search. Picking a command needs one
  extra rule: whether the leading word is a server command.
- **Codex app-server.** In charge of skills (`skills/list` and `$` text) and of file search if
  the client uses `fuzzyFileSearch`. Not in charge of slash commands: every client keeps its own
  table, and those tables already differ (the TUI has about 70 entries, T3 three, codex-acp six).
  A Wheelhouse client for Codex would have to keep a table too, limited to things the
  app-server exposes as methods.
- **T3 and Cursor** go around the harness for Claude skills by reading config directories. T3
  also uses its own file search.

### What a plain-text composer could offer

None of this needs chips. Every harness above takes plain text for commands, and Claude and
Codex take plain text for mentions as well.

1. **A `/` popup over the field, at the start of the message.** Filter the producer's
   advertised list by prefix and show the name, description and argument hint. Choosing an entry
   inserts `/name ` and leaves the cursor for arguments. Sending stays a normal send. The
   producer passes on whatever the harness advertises: Claude `supportedCommands()` minus
   `terminal_slash_commands`, refreshed on `commands_changed`; ACP `availableCommands`; Codex
   skills from `skills/list` as `$name`.
2. **Send commands as text and let the harness interpret them.** This matches Claude and ACP
   directly. For Codex, the producer (not the composer) would map the few command names it
   advertises to app-server methods, the way codex-acp does. The composer still sends text, and
   the harness interface stays the producer's business.
3. **Unknown or unavailable commands.** Don't block them in the composer. Claude answers them
   itself ("isn't available in this environment", or an ordinary message with a note), so the
   reply shows up in the conversation. For producers that reject unknown commands, show the error
   inline as for a failed send.
4. **An `@` popup for files, only where the harness or producer can search.** Ask the producer
   for candidates: Codex `fuzzyFileSearch` with the thread cwd as root, opencode `fs.find`, or
   Claude's search if it becomes public. Insert **plain text**: `@relative/path` for Claude
   (the CLI expands it) and the plain path for Codex. Without a search on the producer, skip
   `@` completion; a typed `@path` still works for Claude. Searching Wheelhouse's own view of the
   filesystem would repeat the T3 and Zed assumption that client and agent share files, which
   fails for remote and crew sessions.
5. **Skills and subagents as plain tokens.** `$skill-name` for Codex and `/skill-name` for
   Claude come from the advertised lists. `@agent-<name>` for Claude subagents could come from
   `supportedAgents()`.
6. **Show what a second client can't run by leaving it out.** Use the harness's own marks:
   `terminal_slash_commands` for Claude, and the adapter's filtering for ACP. Don't keep a
   Wheelhouse deny-list.

So the producer needs to carry an advertised command list (name, description, optional argument
hint, kind such as command, skill or agent) with full-replacement updates. File search is an
optional capability. That is the ACP shape, which Claude's SDK data maps onto directly
(claude-agent-acp does exactly this). Codex needs the producer to supply the command entries.

**Left open for #694.** Whether Wheelhouse commands (acting on the View, not the agent) share
the `/` namespace, as T3's `/model` and Zed's `/login` do, or stay off it. Also whether the
producer should name the trailing-text behaviour of a command, as Zed does for its native
commands, so the composer can tell "queue the rest" from "it's an argument".
