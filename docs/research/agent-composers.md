# Agent composers and in-progress states (researched 2026-11-04)

Scope: how agent chat apps lay out their composer (the input box), which keys and rich
input it takes, what happens when you send during a run, how sending and working show,
and whether drafts persist. It informs the next step for the composer that the prototype
on [#683](https://github.com/flotilla-org/wheelhouse/issues/683) put under the
conversation View (branch `proto/agent-composer`, commit `1e859a8`). That prototype has a
one-line field, Send and Stop, and no busy or pending state. #683 already settled these
points, and this note does not revisit them: input is an AG-UI `RunAgentInput`, the
producer steers or queues input sent mid-run, and approvals are AG-UI interrupts.

**Sources.** For the open-source apps I read the code at these commits (2026-11-03/04):

| App | Repo | Commit |
|---|---|---|
| Codex CLI/TUI | `openai/codex` | `75aba6768a32622f145f97c6ce6c46fd92f4271a` |
| T3 Code | `pingdotgg/t3code` | `d51bc58f5e05123a416a3737ac4bce42d1d76b7c` |
| opencode | `anomalyco/opencode` (`sst/opencode` redirects here) | `fe046564650854594c4c5aa22270f46d9de6cace` |
| Zed | `zed-industries/zed` | `dd3437011469d5c2ec8c821a84cc999979625697` |
| Cline | `cline/cline` | `e68a6ddc2c7a7b4ced07a3ec5947cdb4af3af6e8` |

For the closed apps (Claude Code, claude.ai, the Codex app and IDE extension, Cursor) I
used official docs and changelogs, with URLs given in each section. **[unconfirmed]**
marks a point I couldn't find in a primary source, and the text says what is missing.
Paths below are relative to each repo at the commit above.

---

## Comparison

The table is a summary. The sections below give the detail and sources.

| App | 1. Placement and controls | 2. Keys (send / newline / Esc / stop) | 3. Rich input | 4. Sending while busy | 5. Feedback after sending | 6. Drafts |
|---|---|---|---|---|---|---|
| **Claude Code TUI** | Docked at the bottom (fullscreen renderer). Multi-row. No buttons: model and effort are set by commands. | Enter / Shift+Enter, Ctrl+J, `\`+Enter / Esc interrupts, Esc Esc clears the draft or rewinds / Ctrl+C also stops. Up recalls history per directory. | `[Image #N]` chips, `@` file completion, `/` menu. A paste over 800 chars or 3 lines becomes `[Pasted text #N +X lines]`. | **Queued.** Delivered at the next tool boundary within the turn. Shown grey above the spinner. Up pulls the queue back to edit. Ctrl+Enter sends now. | Message stays grey until the model gets it. Spinner verb, elapsed time and tokens, "esc to interrupt". Live timer on the running tool. | Ctrl+S stash and cleared-draft recall, in-session only. Lost on restart. |
| **claude.ai web/desktop** | Model, effort and thinking menu next to Send; a **+** menu. Growth and centred-to-docked layout **[unconfirmed]**. | Enter sends; the rest **[unconfirmed]**. | Attach documents; the rest **[unconfirmed]**. | No documented queue or steer. | "Thinking" with a timer, then a collapsible summary. Error text "Please try again soon". | **[unconfirmed]** |
| **Codex TUI** | Docked. Grows, up to 2/3 of the screen in fullscreen, then scrolls. Status row and pending-input preview above it, footer with context % below. No buttons. | Enter / Ctrl+J, Shift+Enter / Esc interrupts, Esc Esc backtracks when idle / Ctrl+C. Up recalls history when the draft is empty. | `[Image #N]`, `@` inserts the path as plain text, `/` popup. Over 1000 chars becomes `[Pasted Content N chars]`. | **Enter steers** (`turn/steer`), **Tab queues**. A preview lists pending steers and the queue. Alt+Up pulls the last one back. | Optimistic display, de-duplicated on echo. Shimmer "Working (12s • esc to interrupt)". Unsent and unconfirmed tracked across reconnects. | Per thread in memory. Lost on restart. |
| **Codex app / IDE** | **+** menu, model picker, IDE-context toggle. Growth **[unconfirmed]**. | Enter, or Cmd+Enter by setting. Up on an empty composer recalls. | `/` list, `$` skills, image paste and drop. | Setting **steer or queue**, IDE default `queue`. Cmd+Shift+Enter does the other. Queue above the composer: edit, reorder, send, delete. | "Worked for 6m 53s". Live states **[unconfirmed]**. | **[unconfirmed]** |
| **T3 Code** | Docked Tiptap editor, ≥2 lines, up to `max-h-52` then scroll. Model, effort, mode and plan pickers. **One button that changes role** (Stop / Send / Steer / Queue). | Enter / Shift+Enter (or Mod+Enter by setting). Mod+Enter does the other action. Up recalls this thread's prompts. No default stop key. | Paste and drop files and images. `@` `/` `#` `$` become **chips**. Over 32 KiB becomes a `pasted-text.txt` attachment. | Setting **steer (default) or queue**. Queue list with Send now, Edit and Remove; a row reads "Saving" until accepted. | **Optimistic**, dropped on echo. Spinner on Send. "Working for 0:12", a "Thinking" row, tool groups. On failure the draft is restored and an error banner shows. | **Per thread, persisted** (localStorage) with attachments. |
| **opencode TUI** | Docked textarea, 1 row up to max(6, height/3). No buttons. | Enter / Shift+Enter, Ctrl+J / **Esc twice within 5 s** interrupts. Up recalls history (50 entries). | Ctrl+V image, `@` `/` completion as styled spans. 3+ lines or 150+ chars becomes `[Pasted ~N lines]`. | **Enter steers**, leader+Enter queues. Pending steer is a dimmed message. Queue dock with steer, delete and undo-to-composer. | Clears at once. Spinner and "esc interrupt" hint. Tool labels ("Reading file…"). Toast and restore on failure. | In memory per tab. Explicit stash persisted. |
| **opencode web/desktop** | Docked contenteditable, up to 180 px then scroll. **+**, model, variant. **One Send/Stop button.** | Enter / Shift+Enter. Mod+Enter does the other action. Single Esc stops. Shift+Up searches history. | Paste and dropzone, `@` **pills**, `/` menu. Long paste inserted as plain text. | Setting **steer (default) or queue**. Queue panel: reorder, steer, undo, remove. | Steer shown before the composer clears. Running state set at once. Restore and retry on failure. | **Per session, persisted.** |
| **Zed agent panel** | Docked, 4–8 lines then scroll. Expand to a full editor. Model, profile and mode pickers. **One button: Stop / Queue and Send / Send.** | Enter (or Cmd+Enter by setting). Esc cancels generation. Cmd+Shift+Enter interrupts and sends. Up pulls back a queued message; there is no history. | `@` **creases** (chips) for files, symbols, threads, diagnostics and more. `/` completion. Image paste and drop. No paste collapse. | **Queued**, sent at turn end. List above the composer: Edit, Send Now, Remove, per-item **Steer** toggle. Enter on an empty composer sends the next one. | Optimistic for its own agent. External agents show "Sending…"/"Waiting…" **after 1 s** until echoed. Dots spinner. Optional elapsed time and tokens. Error row with Retry. | Persisted for new threads only. Follow-ups **[unconfirmed]**. |
| **Cline (VS Code)** | Docked, 3–10 rows then scroll. Separate Cancel button. | Enter / Shift+Enter. No history, no stop key. | `@` as highlighted plain text, `/` menu, image paste, Shift+drag. | **Queued**. Rows truncated with a "Steer" badge. Cancel only. | Optimistic, rolled back on error. "Thinking..." after 500 ms. Error row with Retry. | One webview draft. Persistence **[unconfirmed]**. |
| **Cursor** | Side pane, pills above the prompt. Send button swaps to Stop (a setting can turn this off). | Enter. Cmd+Enter force-sends. Cmd+Shift+Backspace cancels. | `@` chips, `/` picker, paste and drop. | Changed over time: queue, then **steer** on fresh installs. Queue can be edited and reordered. | "Worked for …" per turn. Other states **[unconfirmed]**. | Partly: actions keep the draft. |

---

## Claude Code TUI

Sources: [interactive mode](https://code.claude.com/docs/en/interactive-mode),
[fullscreen](https://code.claude.com/docs/en/fullscreen),
[terminal config](https://code.claude.com/docs/en/terminal-config),
[settings](https://code.claude.com/docs/en/settings-reference),
[desktop](https://code.claude.com/docs/en/desktop),
[CHANGELOG](https://github.com/anthropics/claude-code/blob/main/CHANGELOG.md) (versions as cited).

- **Layout.** In the fullscreen renderer the input box "stays fixed at the bottom of the
  screen" while output streams. The docs don't state auto-grow or a maximum height, but Up and
  Down move between rows "when the input spans more than one visual row", so the box has
  several rows. There are no buttons: `/model`, `/effort`, Option+T for thinking, and
  Ctrl+G for an external editor.
- **Keys.**
  - Enter sends. Ctrl+J and `\`+Enter make a newline everywhere; Shift+Enter works only in terminals that support it.
  - Up and Down move between rows first, then through history, which is kept per working directory. Ctrl+R searches it.
  - Esc interrupts, keeps the work done so far, and sends any queued messages.
  - Esc Esc clears a non-empty draft, saving it to history. On an empty draft it opens the rewind menu.
  - Ctrl+C interrupts too.
- **Rich input.**
  - Ctrl+V pastes an image, which shows as an `[Image #N]` chip.
  - `@` completes file paths and `/` opens commands.
  - A paste over 800 characters or 3 lines collapses to `[Pasted text #1 +120 lines]`. The full text is sent, and the placeholder is deleted or restored as one unit.
- **Busy.** You can type and press Enter at any time; the message is queued.
  - When the running tool calls finish, the queued message is passed to Claude within the same turn. Slash and `!` commands wait for the turn to end.
  - Queued messages are listed above the spinner in grey (2.1.275, 2.1.281).
  - Up from the first line pulls them back into the box to edit.
  - Ctrl+Enter sends now, and in fullscreen the "to send now" hint can be clicked (2.1.312).
- **Feedback.**
  - Messages stay grey "until the model receives them".
  - The spinner shows a verb (configurable), elapsed time, a token count including subagents (2.1.0) and "esc to interrupt".
  - The running tool header has a live timer (2.1.210). A turn ends with a "Cooked for …" duration.
  - I found no documented retry for a failed send.
- **Drafts.** Ctrl+S stashes a draft. The last ten drafts cleared with Ctrl+C come back with Up, and they are "discarded when you exit" (2.1.292). History persists, but drafts don't survive a restart.
- **Desktop "Code" tab.**
  - Pickers sit in a row under the prompt, and a **+** button attaches. Enter sends, Esc or a Stop button stops.
  - A correction typed during a run is read "as soon as the current action completes".
  - 2.1.299 fixed a VS Code bug that lost an unsent prompt on switching sessions, so drafts are meant to be kept per session there.

## claude.ai (web and desktop chat)

Official help pages say little about the composer.

- **Confirmed.**
  - A model menu "next to the send button" sets model, effort and thinking. A change applies from the next response ([8664678](https://support.claude.com/en/articles/8664678)).
  - A **+** menu holds connectors, search and similar ([tutorial](https://academy.claude.com/tutorials/getting-started-with-claude-ai)).
  - Enter sends ([Quick Entry](https://support.claude.com/en/articles/12626668)).
  - While thinking it shows "Thinking" with a timer, then a collapsible summary ([10574485](https://support.claude.com/en/articles/10574485)).
  - Capacity errors read "Please try again soon" ([12466728](https://support.claude.com/en/articles/12466728-understand-claude-error-messages)).
- **[unconfirmed].** These are widely seen but I found no official page for them:
  - Shift+Enter for a newline;
  - a centred new-chat box that docks to the bottom after the first message;
  - auto-grow;
  - Send turning into Stop;
  - no sending during a reply;
  - per-conversation drafts.

  The [release notes](https://support.claude.com/en/articles/12138966-release-notes) mention no queue or steer feature.

## OpenAI Codex

### TUI (open source, `codex-rs/tui/src/`)

- **Layout.**
  - `bottom_pane/mod.rs` is "the interactive footer": the composer (`bottom_pane/chat_composer.rs`) plus popups. Above it sit the status row (`status_indicator_widget.rs`) and the queued-input preview (`bottom_pane/pending_input_preview.rs`). Below it, `footer.rs` shows "N% context left".
  - The composer grows up to 2/3 of the screen in fullscreen (`app/owned_transcript.rs:345`), then scrolls with ↑/↓ markers in the margin. I didn't confirm a cap in inline mode.
- **Keys** (`keymap.rs`, `built_in_defaults`).
  - Enter submits and Tab queues. Ctrl+J, Shift+Enter and Alt+Enter make a newline.
  - Up and Down recall history only when the draft is empty or still holds the recalled entry (`chat_composer_history.rs`). Ctrl+R searches.
  - Esc or Ctrl+C interrupts.
  - Esc Esc on an idle, empty composer starts backtrack: choose an earlier prompt, revert the thread to that point and edit the prompt (`app_backtrack.rs`).
- **Rich input.**
  - `LARGE_PASTE_CHAR_THRESHOLD: usize = 1000` (`chat_composer.rs:471`): larger pastes become an atomic `[Pasted Content N chars]` element, expanded on submit.
  - Ctrl+V pastes an image as `[Image #N]`. A dropped image path also becomes an attachment.
  - `@` opens a popup. A chosen file goes in as **plain path text**, while plugins and skills go in as atomic elements. `/` opens a completion popup.
- **Busy.** Enter during a run steers.
  - `input_submission.rs:253` checks `agent_turn_running`, and the app server receives `turn/steer` (`app_server_session.rs`). Tab queues for the next turn.
  - `pending_input_preview.rs` shows "Messages to be submitted after next tool call (press Esc to interrupt and send immediately)", then "Messages to be submitted at end of turn", then the queue, with up to 3 lines per item.
  - Alt+Up (`edit_queued_message`) pulls the last queued message back.
  - Esc with steers pending interrupts the turn and resubmits them as a new turn.
- **Feedback.**
  - The prompt is shown optimistically ("Queue the optimistic prompt first so the user's input is visible", `input_submission.rs`). A compare key stops a duplicate row when the server echoes it.
  - The status row is a shimmering "Working" header with "(12s • esc to interrupt)" and up to 3 detail lines.
  - After a reconnect, messages are tracked as `Unsent` or `Unconfirmed` (`chatwidget/reconnect.rs`). A blocked submission restores the draft.
- **Drafts.** Each thread keeps its composer, queue and steers in memory across switches (`ThreadInputState`, `chatwidget/user_messages.rs`). Nothing is written to disk; `startup_recovery.rs` keeps only an in-memory draft for startup errors.

### Desktop app and IDE extension (closed)

developers.openai.com/codex now redirects to learn.chatgpt.com.

- **Busy.** "Follow-up behavior" is a setting with two values, steer or queue ([settings](https://learn.chatgpt.com/docs/reference/settings)). In the IDE it is `chatgpt.followUpQueueMode`, default `queue`, and a legacy `interrupt` value is treated as `steer` ([developer settings](https://learn.chatgpt.com/docs/developer-settings)). Cmd/Ctrl+Shift+Enter flips the behaviour for one message.
- **Queue.** "Queued messages appear above the composer, where you can edit, reorder, send, or delete them" ([prompting](https://learn.chatgpt.com/docs/prompting)).
- **Keys.** Enter sends, or Cmd+Enter when `chatgpt.composerEnterBehavior` says so. Up recalls the last prompt when the composer is empty.
- **Rich input.** `/` lists commands and `$` lists skills; images can be pasted or dropped.
- **[unconfirmed].** Live indicators, auto-grow, whether send and stop share a button, and draft persistence. A community thread reports the follow-up setting being ignored.

## T3 Code (`apps/web/src/…`)

- **Layout.**
  - `ChatComposer` is docked below the timeline in `ChatView`. The editor is Tiptap (`components/ComposerPromptEditorTiptap.tsx`): at least 2 lines, up to `max-h-52`, then it scrolls.
  - `ComposerFooterControls` holds the model and effort pickers, the runtime and plan modes, a context meter and a stash menu.
  - `ComposerPrimaryActions.tsx` is **one button whose role changes**: Stop when the agent can be interrupted and the draft is empty, otherwise Send, Steer, Queue, "Update queued message" or Resume.
- **Keys** (`composer-logic.ts`, `packages/shared/src/keybindings.ts`).
  - Enter sends and Shift or Alt+Enter makes a newline. The `sendShortcut` setting can make Mod+Enter the send key.
  - Mod+Enter during a run does the other action (steer or queue).
  - Up recalls this thread's prompts when the draft is empty and the caret is on the edge line.
  - Esc only closes menus. `thread.stop` has no default key.
- **Rich input.**
  - Paste and drop images and files (`useComposerAttachmentIntake.ts`).
  - `@` paths, `/` commands, `#` PRs and `$` skills become atomic chip nodes.
  - A paste over `PASTED_TEXT_ATTACHMENT_THRESHOLD_BYTES = 32 * 1024` (`packages/client-runtime/src/textPaste.ts`) becomes a `pasted-text.txt` attachment.
- **Busy.**
  - `resolveComposerDispatchMode` (`packages/client-runtime/src/state/composerDispatch.ts`) picks between steer and queue using `followUpBehavior`, which defaults to `"steer"` (`packages/contracts/src/settings.ts:474`). During compaction it always queues.
  - `QueuedRunsControl.tsx` shows the first 3 queued items. Each has Send now, Edit and Remove, and reads "Saving" until the server accepts it.
- **Feedback.**
  - The user message shows at once and is dropped when the server echoes it (`optimisticUserMessages.ts`). Send shows a spinner while the request is in flight.
  - The timeline shows "Working for …", a "Thinking" row with a shimmer, tool groups and "Compacting for…" (`TimelineWorkGroupRows.tsx`, `ThreadStatusLine.tsx`).
  - On failure the optimistic message is removed, the draft is restored (`restoreComposerDraft`), and an error banner or toast appears.
- **Drafts.** Kept per thread with attachments, in a zustand store persisted to localStorage under `t3code:composer-drafts:v1` (`composerDraftStore.ts`).

## opencode

**TUI** (`packages/tui/src/`)

- **Layout.** `component/prompt/index.tsx` is a docked textarea, `minHeight` 1 and `maxHeight = max(6, floor(height/3))`. There are no buttons.
- **Keys.**
  - Enter steers and `<leader>Enter` queues.
  - Esc interrupts only on a second press within 5 s; the hint changes to "esc again to interrupt".
  - History is kept in `prompt-history.jsonl`, up to 50 entries.
- **Rich input.** A paste of 3 or more lines or over 150 characters collapses to `[Pasted ~N lines]`.
- **Busy.** A pending steer shows as a dimmed user message. A queue dock ("N queued · …") offers steer, delete, and undo back into the composer.
- **Feedback.** A spinner in the agent's colour and per-tool labels show progress. A failure shows a toast and restores the text.
- **Drafts.** Kept in memory per tab (`draft-stash.ts`), so they don't survive a restart.

**Web and desktop** (`packages/app/src/composer/`)

- **Layout.**
  - A contenteditable editor scrolls past 180 px.
  - `ComposerEditorSubmitButton` is one button. It shows Stop when `working && blank` (`model.ts:121`), otherwise Send, and a loader while held.
- **Keys.** Enter sends, Shift+Enter makes a newline, and Mod+Enter does the other action. A single Esc stops.
- **Rich input.** `@` mentions become non-editable pills.
- **Busy.** `followUpBehavior` defaults to `steer` (`settings/model.tsx`). `session/composer/queue-panel.tsx` lets you reorder, steer, undo and remove queued prompts.
- **Feedback.** A queued prompt appears only once the server accepts it (`submit.ts`). A failure restores the draft and sets up a retry.
- **Drafts.** Persisted per session through `Persist.prompt`, in localStorage or the desktop store.

## Zed agent panel (`crates/agent_ui/src/…`)

- **Layout.**
  - Docked under the thread (`conversation_view/thread_view.rs`, `render_message_editor`).
  - It grows from `message_editor_min_lines` (4) to twice that, then scrolls. `agent::ExpandMessageEditor` switches to a full editor at 80% of the height.
  - **One button**: Stop when generating with an empty editor, "Queue and Send" when generating with text, Send otherwise.
- **Keys.**
  - Enter sends, or Cmd+Enter with `use_modifier_to_send`.
  - Esc cancels generation. Cmd+Shift+Enter (`SendImmediately`) interrupts and sends.
  - Up in an empty composer edits the first queued message. There is no prompt history; you edit an earlier prompt by clicking it.
- **Rich input.**
  - `@` mentions become creases (`ui/mention_crease.rs`, `acp_thread/src/mention.rs`) for files, symbols, threads, diagnostics, selections, diffs and images.
  - `/` completes commands (`completion_provider.rs`).
  - Images can be pasted and files dropped.
  - Long pastes aren't collapsed, except that a code selection becomes a crease.
- **Busy.**
  - `conversation_view/message_queue.rs`: input sent during a run is **queued** and sent when the turn ends, and the queue pauses if you stop the run.
  - The queue shows above the composer, labelled "Next in Queue" and "In Queue". Each item has Edit, Send Now, Remove and a **Steer** toggle; steer works only for Zed's own agent.
  - Enter on an empty composer sends the next queued item.
- **Feedback.**
  - For an external agent, a message not yet echoed shows "Sending…" or "Waiting…", but only after `UNSETTLED_SUBMISSION_REVEAL_DELAY = 1s` (`thread_view.rs:76`), so a fast echo never flashes.
  - `render_generating` shows a dots spinner, or "Awaiting Confirmation" while a tool waits for approval.
  - With `show_turn_stats`, it adds elapsed time after 30 s and tokens after 250.
  - `render_thread_error` shows a Retry button, plus "Retrying. Next attempt in N seconds".
- **Drafts.** `draft_prompt_store.rs` persists the draft only for threads with no messages yet.

## Cline (`apps/vscode/webview-ui/src/components/chat/…`)

- **Layout.** The textarea autosizes from `minRows={3}` to `maxRows={10}`. Send is an icon, and Cancel is a separate button (`ActionButtons.tsx`).
- **Keys.** Enter sends and Shift+Enter makes a newline. There is no history and no stop key.
- **Rich input.** `@` mentions are plain text with a highlight overlay. A `/` menu completes commands. Images can be pasted, and files dragged in with Shift held.
- **Busy.** Input sent during a run is queued (`sdk-followup-coordinator.ts`). `QueuedPrompts.tsx` lists the items, which can only be cancelled.
- **Feedback.** An optimistic `pendingUserMessage` rolls back on error and restores the draft. "Thinking..." appears after `THINKING_LOADER_GRACE_MS = 500`. Errors show an error row with Retry.
- **Drafts.** There is one draft snapshot in webview state. I found no persistence.

## Cursor (closed)

Sources: [changelog 1.4](https://cursor.com/changelog/1-4), [release notes](https://cursor.com/docs/release-notes),
[agent overview](https://cursor.com/docs/agent/overview), [shortcuts](https://cursor.com/docs/configuration/kbd).

- **Busy.** Version 1.4 added queued messages: ⌥Enter queues and ⌘Enter interrupts. The docs now say Enter queues, and "Send now" or a double Enter steers at the next tool call. The queue can be edited and reordered.
- **Steer default.** Release notes say fresh installs now steer by default. I couldn't pin that note to a version.
- **Send and Stop.** A 3.27 setting lets you "keep the Send button instead of swapping it for Stop", so by default one button swaps between them.
- **Stop key.** Cmd+Shift+Backspace cancels generation.
- **Feedback.** Each finished turn shows how long the agent worked.
- **[unconfirmed].** Composer growth, optimistic display, and how errors show.

---

## Synthesis

### Baseline: what all of them do

1. **The composer is docked at the bottom of the conversation** and stays put while output
   streams. It is multi-line and **grows with its content to a cap**, then scrolls. The cap
   is about 4–10 lines, 180–208 px, or a third to two thirds of a terminal.
2. **Enter sends and a modified Enter inserts a newline.** Shift+Enter is the common one; TUIs
   also accept Ctrl+J because terminals often can't report Shift+Enter. GUIs offer a setting
   to swap to Cmd+Enter.
3. **You can stop from the keyboard.** In most of them Esc stops the run. Cline has no stop key,
   Cursor uses Cmd+Shift+Backspace, and T3 Code relies on the button.
4. **You can always type during a run**, and every app that runs agents accepts the input.
   None blocks the field. claude.ai chat is the possible exception **[unconfirmed]**.
5. **A sent message shows at once** (optimistic), and the echo de-duplicates it. Codex, T3,
   Cline, opencode and Zed's own agent do this. Where delivery isn't certain, the message is
   marked: Claude Code shows it grey until received, Zed shows "Sending…" or "Waiting…",
   Codex tracks `Unsent` and `Unconfirmed`.
6. **A run in progress shows as a live row at the end of the thread or above the composer.** The
   row has a spinner or shimmer and a short label ("Working", "Thinking"). Most add elapsed time
   (Claude Code, Codex, T3) and an interrupt hint.
7. **A failed send gives the text back.** The draft is restored (T3, Codex, opencode, Cline),
   and an error row or toast appears, often with Retry.
8. **Up on an empty composer recalls something**: prompt history (Claude Code, Codex, T3,
   opencode) or the last queued message (Zed, Claude Code).

### Real choices: where they differ

- **One button or two.** Most GUIs use one primary button whose role changes: Stop when busy
  and the draft is empty, Send, Steer or Queue when there is text (T3, opencode web, Zed,
  Cursor). Cline keeps Send and Cancel separate. A one-line rule decides between them:
  `busy && draft empty → Stop`.
- **Steer or queue by default.** Codex TUI, opencode, T3 and newer Cursor steer by default,
  and so does Claude Code in effect: its queue is delivered at the next tool boundary. Zed,
  Cline and the Codex IDE queue by default. Almost everyone offers the other behaviour on a
  modifier (Tab, Mod+Enter, leader+Enter) and a **Send now** that interrupts and sends. For
  Wheelhouse, #683 leaves this to the producer.
- **Whether queued items can be edited.** The options range from cancel-only (Cline), to
  pulling the item back into the composer (Codex Alt+Up, Claude Code Up, opencode undo), to a
  full list with edit, reorder, send now and remove (Zed, T3, Codex IDE, opencode web).
- **Single or double Esc to stop.** Most stop on one Esc. opencode TUI asks for two presses
  within 5 s, guarding against a stray key. Esc Esc also means rewind or backtrack in Claude
  Code and Codex.
- **How mentions show.** Chips or creases (Zed, T3, opencode web, Cursor), atomic text
  elements (Codex skills and plugins, Claude Code images and pastes), or plain path text
  (Codex files, Cline).
- **How long pastes are handled.** Collapse to a placeholder (Claude Code over 800 chars or
  3 lines, Codex over 1000 chars, opencode TUI over 150 chars or 3 lines), turn into an
  attachment (T3 over 32 KiB), or leave it alone (Zed, Cline, opencode web).
- **How long drafts last.** In memory per thread (Codex, opencode TUI), persisted per thread
  (T3, opencode web), or session-only stashes (Claude Code).
- **How much "working" detail to show.** Options are the elapsed time, a token count and the
  current tool's name. Zed hides elapsed time below 30 s and holds back "Sending…" for 1 s,
  so fast cases don't flicker.

### Recommendation: the minimal next step for Wheelhouse

All of this works with a plain multi-line field and a few event-driven states. None of it
needs editable Rich Text.

1. **The field.** Make it a docked, multi-line plain-text field that grows from 1 line to about
   8, then scrolls.
   - Enter sends and Shift+Enter inserts a newline. Accept Ctrl+J as well, the TUI fallback
     that costs nothing.
   - Up on an empty field recalls the last sent prompt. Session-only history is enough.
2. **One primary button.** Show **Stop** when a run is open and the field is empty, otherwise
   **Send**. Esc in the field stops an open run, with a single press like most apps.
   Cmd+Enter stays free for a later "send now".
3. **A pending prompt.** On Send, add the user message to the chat at once, marked *pending*
   (dimmed). Key it by the `RunAgentInput` message id and clear the mark when the producer
   echoes that id, the matching #683 already requires. To tell a slow send from a slow reply,
   show three states:
   - **Sending.** The POST is in flight; the button shows a spinner.
   - **Sent, not yet echoed.** After about 1 s, as in Zed, show a subtle "Waiting for agent…".
   - **Echoed.** Show the normal bubble.

   If the POST fails, remove the pending bubble, **put the text back in the field**, and show
   an inline error with Retry.
4. **A busy row.** While a run is open (`RunStarted` with no `RunFinished` yet), show one live
   row at the end of the conversation: a spinner, "Working", the elapsed time, and **the title
   of the tool call in flight** (from the last `ToolCallStart` without an end), plus
   "Esc to stop". A finished run can leave a quiet "Worked for 1m 12s". This needs nothing
   beyond the event stream.
5. **Prompts sent during a run.** Allow sending any time and show the pending bubble. The
   producer steers or queues it (#683), so Wheelhouse needs no queue of its own yet. A
   steered prompt that isn't echoed for a while simply stays pending, which is honest.
   Editing and reordering a producer-side queue can come later, if producers advertise one.
6. **Drafts.** Keep the draft per conversation in memory, so switching Views doesn't lose it.
   Persisting drafts across restarts can wait.

**Deferred to editable Rich Text:** mention chips, collapsing long pastes into placeholders,
image paste and drop as inline attachments, and a `/` command popup with completion. A
plain field can still take a pasted file path as text in the meantime.

**Left open:** whether Esc should need a double press, as in opencode, given that the field
shares the window with a terminal View. Also whether elapsed time should be hidden for short
runs, as Zed does.
