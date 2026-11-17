#!/usr/bin/env node
// Prototype (#683): a Codex app-server client that serves a thread and its subagents'
// threads over the Agent View protocol (#691): Wheelhouse's conversation contract
// (#548, version 2) as a watch stream on a unix socket, and prompts in. Throwaway;
// Flotilla's connectors own live producers.
//
//   node tools/codex-agent-host.mjs --url ws://127.0.0.1:PORT (--new [--cwd DIR] | --thread ID) --socket PATH [--buffer N] [--page BYTES]
//     [--approval untrusted|on-request|never]
//
// GET /threads, GET /threads/{id}/events (?after=N, ?before=N&limit=M), POST
// /threads/{id}/run and /stop, POST /threads/{id}/tasks/{taskId}/stop, GET
// /threads/{id}/tasks/{taskId}/output and GET /threads/{id}/files?q=…&limit=….
// stdin still works: a line is a prompt; /interrupt stops the turn; y or n
// answers a pending approval.
import { unlinkSync } from 'node:fs';
import { createServer } from 'node:http';
import { createInterface } from 'node:readline';

const args = process.argv.slice(2);
const opt = (name) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : undefined; };
const count = (name, fallback) => {
  const n = Number(opt(name) ?? fallback);
  if (!(n > 0)) throw new Error(`${name} takes a positive number`);
  return n;
};
const url = opt('--url') ?? 'ws://127.0.0.1:4599';
const socket = opt('--socket') ?? 'codex-agent.sock';
const cwd = opt('--cwd') ?? process.cwd();
// How far back `?after=` can resume; older starts over with a snapshot.
const buffer = count('--buffer', 100000);
// A snapshot's newest page, at least this many bytes of whole runs.
const pageBytes = count('--page', 256 * 1024);
// Codex's approval policy for the turns it starts: untrusted, on-request or never.
const approvalPolicy = opt('--approval');

// ---- each thread's events, numbered by their place in its stream. Numbers
// start past any an earlier bridge gave out, so a watcher resuming from one is
// sent a snapshot rather than this log's events at its numbers.
const base = Date.now() * 1024;
// The served thread first, then its subagents' as Codex spawns them.
const threads = new Map();
let main = null;

function addThread(id, parent) {
  const thread = { id, parent, log: [], watchers: new Set(), documents: new Map(), status: null,
    turnId: null, runId: null, runOpen: false, runs: 0, open: new Set(), terminals: new Map(), waits: new Map(),
    queued: [], tasks: new Map(), title: null };
  threads.set(id, thread);
  return thread;
}

const emit = (thread, type, fields) => {
  const event = { type, threadId: thread.id, seq: base + thread.log.length + 1, ...fields };
  const line = JSON.stringify(event) + '\n';
  const record = { event, line };
  thread.log.push(record);
  for (const res of thread.watchers) res.write(line);
  return record;
};

// State documents by key, each sent whole. A snapshot restates every one.
const publish = (thread, key, mediaType, snapshot) => { thread.documents.set(key, emit(thread, 'StateSnapshot', { key, mediaType, snapshot })); };
const setStatus = (thread, value) => {
  if (value === thread.status) return;
  thread.status = value;
  publish(thread, 'status', 'application/json', value);
};

// Items a snapshot's earlier count and a page's limit count, as Wheelhouse does.
const STARTS = new Set(['TextMessageStart', 'ToolCallStart', 'ReasoningMessageStart', 'Custom']);
const itemsIn = (thread, from, to) => thread.log.slice(from, to).filter(({ event }) => STARTS.has(event.type)).length;
// Snapshots and pages cut only where a turn starts, so an open turn and its
// open items always come whole; the runs an Interrupt cuts it into don't count.
const runStart = (thread, i) => { while (i > 0 && !thread.log[i].turn) i--; return i; };

const route = (thread, ...parts) => ['', 'threads', thread.id, ...parts].map(encodeURIComponent).join('/');

// A subagent takes no direct input from the app-server's clients; only its
// approvals are answered.
const hello = (thread, snapshot) => ({ type: 'Hello', contract: 2, threadId: thread.id,
  capabilities: thread === main
    ? { input: true, modes: ['steer', 'queue'], commands: true, roots: [{ path: cwd, local: true, search: route(thread, 'files') }] }
    : { input: false, roots: [{ path: cwd, local: true, search: route(thread, 'files') }] },
  ...(snapshot ? { snapshot } : {}) });

function watch(thread, res, query) {
  res.writeHead(200, { 'Content-Type': 'application/jsonl', 'Cache-Control': 'no-store' });
  const { log } = thread;
  const after = query.has('after') ? Number(query.get('after')) - base : NaN;
  const write = (event) => res.write(JSON.stringify(event) + '\n');
  if (after >= Math.max(0, log.length - buffer) && after <= log.length) {
    write(hello(thread));
    for (const { line } of log.slice(after)) res.write(line);
  } else {
    let start = log.length, bytes = 0;
    while (start > 0 && bytes < pageBytes) bytes += log[--start].line.length;
    start = runStart(thread, start);
    write(hello(thread, { earlierItems: itemsIn(thread, 0, start), oldestSeq: base + start + 1 }));
    for (const record of thread.documents.values()) if (log.indexOf(record) < start) res.write(record.line);
    for (const { line } of log.slice(start)) res.write(line);
  }
  thread.watchers.add(res);
  // The response's close, not the request's: that fires once its body is read.
  res.on('close', () => thread.watchers.delete(res));
}

function page(thread, query) {
  const { log } = thread;
  const before = Math.max(0, Math.min(Number(query.get('before') ?? base + log.length + 1) - base - 1, log.length));
  const asked = Math.trunc(Number(query.get('limit') ?? 50));
  const limit = Math.min(asked > 0 ? asked : 50, 500);
  let start = before;
  while (start > 0 && itemsIn(thread, start, before) < limit) start--;
  start = runStart(thread, start);
  return { events: log.slice(start, before).map(({ event }) => event), earlierItems: itemsIn(thread, 0, start), oldestSeq: base + start + 1 };
}

const KINDS = { commandExecution: 'shell', fileChange: 'edit', mcpToolCall: 'mcp', dynamicToolCall: 'other',
  webSearch: 'web', collabAgentToolCall: 'agent', subAgentActivity: 'agent', imageView: 'read' };
const userText = (item) => (item.content ?? []).filter((c) => c.type === 'text').map((c) => c.text).join('\n');
const reasoningText = (item) => [...(item.summary ?? []), ...(item.summary?.length ? [] : item.content ?? [])]
  .map((s) => (typeof s === 'string' ? s : s.text ?? '')).join('\n\n');
const shorten = (text) => { const t = text.split(/\s+/).filter(Boolean).join(' '); return t.length <= 120 ? t : `${t.slice(0, 119)}…`; };

function toolTitle(item) {
  switch (item.type) {
    case 'commandExecution': return item.command;
    case 'fileChange': return (item.changes ?? []).map((c) => c.path).join(', ');
    case 'mcpToolCall': return `${item.server}.${item.tool}`;
    case 'dynamicToolCall': return item.tool;
    case 'webSearch': {
      const a = item.action ?? {};
      if (a.type === 'search') return (a.queries ?? [a.query]).filter(Boolean).join(' · ') || item.query;
      if (a.type === 'openPage') return a.url;
      if (a.type === 'findInPage') return `“${a.pattern}” in ${a.url}`;
      return item.query;
    }
    case 'imageView': return item.path;
    case 'collabAgentToolCall': return item.prompt ? `${item.tool}: ${shorten(item.prompt)}` : item.tool;
    case 'subAgentActivity': return `${item.kind}: ${item.agentPath}`;
    default: return item.tool ?? item.type;
  }
}

function diffOf(item) {
  return (item.changes ?? []).map((c) => {
    const d = c.diff ?? '';
    return d.startsWith('---') || d.startsWith('diff ') ? d : `--- a/${c.path}\n+++ b/${c.path}\n${d}`;
  }).join('\n');
}

function toolResult(item) {
  const failed = item.status === 'failed' || item.status === 'declined' ||
    (item.type === 'commandExecution' && item.exitCode != null && item.exitCode !== 0) || item.success === false;
  const status = failed ? 'failed' : item.status === 'interrupted' ? 'cancelled' : 'ok';
  const base = { toolCallId: item.id, messageId: `${item.id}-result`, status,
    ...(item.type === 'webSearch' ? { title: toolTitle(item) || undefined } : {}) };
  switch (item.type) {
    case 'commandExecution': return { ...base, mediaType: 'text/x-ansi', content: item.aggregatedOutput ?? '' };
    case 'fileChange': return { ...base, mediaType: 'text/x-diff', content: diffOf(item) };
    case 'mcpToolCall': return { ...base, mediaType: 'text/plain', content: JSON.stringify(item.error ?? item.result ?? null, null, 2) };
    case 'webSearch': return { ...base, mediaType: 'text/markdown', content: (item.results ?? []).map((r) =>
      `- [${(r.title || r.url).replace(/[[\]]/g, '')}](${r.url}) · ${r.domain ?? ''}${r.snippet ? `\n  ${r.snippet.replace(/\s+/g, ' ')}` : ''}`).join('\n') };
    case 'collabAgentToolCall': return { ...base, mediaType: 'text/plain', content: Object.entries(item.agentsStates ?? {})
      .map(([id, s]) => `${id}: ${s.status}${s.message ? `\n${s.message}` : ''}`).join('\n\n') };
    case 'subAgentActivity': return { ...base, mediaType: 'text/plain', content: item.agentThreadId };
    default: return { ...base, mediaType: 'text/plain', content: JSON.stringify(item.contentItems ?? item.results ?? '', null, 2) };
  }
}

// ---- the checklist: Codex's update_plan sends the whole plan, as its own
// notification rather than an item, so a thread's history has none.
const STEPS = { pending: 'pending', inProgress: 'in_progress', completed: 'completed' };
const publishChecklist = (thread, plan) => publish(thread, 'checklist', 'application/vnd.flotilla.checklist+json',
  plan.map((p, i) => ({ id: String(i + 1), content: p.step, status: STEPS[p.status] ?? 'pending' })));

const OUTPUT_KEPT = 1 << 20;

// ---- tasks: subagents, and terminals left running in the background. A task
// keeps its final status once it ends.
const publishTasks = (thread) => publish(thread, 'tasks', 'application/vnd.flotilla.tasks+json',
  [...thread.tasks.values()].map(({ processId, stopping, ...task }) => task));
const AGENT_ENDS = { completed: 'completed', errored: 'failed', interrupted: 'cancelled', shutdown: 'completed' };
function setTask(task, status, when = Date.now()) {
  if (task.status === status) return false;
  // Asked again after it ended, a subagent runs again.
  if (status === 'running') delete task.endedAt;
  else task.endedAt = when;
  task.status = status;
  return true;
}

// A subagent's thread, spawned by a call in this one; its notifications may
// come before the call that names it, so they wait for it.
const early = new Map();
function spawned(parent, item, childId, when) {
  let changed = false;
  if (!parent.tasks.has(childId)) {
    parent.tasks.set(childId, { id: childId, kind: 'subagent', title: shorten(item.prompt ?? item.agentPath ?? childId),
      status: 'running', startedAt: when, toolCallId: item.id, childThreadId: childId, awaited: false });
    changed = true;
  }
  if (!threads.has(childId)) {
    const child = addThread(childId, { threadId: parent.id, toolCallId: item.id });
    setStatus(child, 'idle');
    for (const [method, params] of early.get(childId) ?? []) onNotification(method, params);
    early.delete(childId);
  }
  return changed;
}

function agentItem(thread, item, when) {
  let changed = false;
  if (item.type === 'subAgentActivity') {
    if (item.kind === 'started') changed = spawned(thread, item, item.agentThreadId, when);
    else if (thread.tasks.has(item.agentThreadId)) {
      const status = { interacted: 'running', interrupted: 'cancelled', completed: 'completed' }[item.kind];
      if (status) changed = setTask(thread.tasks.get(item.agentThreadId), status, when);
    }
  } else {
    if (item.tool === 'spawnAgent') for (const id of item.receiverThreadIds ?? []) changed = spawned(thread, item, id, when) || changed;
    for (const [id, state] of Object.entries(item.agentsStates ?? {})) {
      const task = thread.tasks.get(id);
      const status = state.status === 'running' || state.status === 'pendingInit' ? 'running' : AGENT_ENDS[state.status];
      if (task && status) changed = setTask(task, status, when) || changed;
    }
  }
  if (changed) publishTasks(thread);
}

// A terminal is background work once its call has returned and it still runs:
// its turn ended, or the model wrote to it.
function backgrounded(thread, itemId) {
  const command = thread.terminals.get(itemId);
  if (!command?.processId || thread.tasks.has(itemId) || !thread.open.has(itemId)) return;
  thread.tasks.set(itemId, { id: itemId, kind: 'shell', title: command.command, status: 'running', startedAt: command.startedAt,
    toolCallId: itemId, awaited: false, processId: command.processId,
    output: { url: route(thread, 'tasks', itemId, 'output'), mediaType: 'text/x-ansi' } });
  publishTasks(thread);
}

// Live items open on item/started, grow by deltas and close on item/completed;
// history items arrive closed. Both go through these two.
function itemStarted(thread, item, when = item.startedAtMs) {
  if (thread.open.has(item.id)) return;
  if (item.type === 'agentMessage') {
    thread.open.add(item.id); emit(thread, 'TextMessageStart', { messageId: item.id, role: 'assistant' });
  } else if (item.type === 'reasoning') {
    // Reasoning opens on its first text: Codex often sends none, and an empty
    // Block would show as a gap.
  } else if (KINDS[item.type]) {
    thread.open.add(item.id);
    // A web search's query arrives only when it completes, as a later title.
    emit(thread, 'ToolCallStart', { toolCallId: item.id, toolCallName: item.type, title: toolTitle(item) || undefined, kind: KINDS[item.type] });
    const input = item.type === 'commandExecution' ? { command: item.command, cwd: item.cwd }
      : item.type === 'collabAgentToolCall' ? { tool: item.tool, prompt: item.prompt } : item.arguments ?? {};
    emit(thread, 'ToolCallArgs', { toolCallId: item.id, delta: JSON.stringify(input) });
    emit(thread, 'ToolCallEnd', { toolCallId: item.id });
    if (item.type === 'commandExecution') thread.terminals.set(item.id, { command: item.command, processId: item.processId, startedAt: when ?? Date.now(), output: '' });
  }
}
function itemCompleted(thread, item, live, when = item.completedAtMs) {
  if (item.type === 'userMessage') {
    // A prompt keeps the id its sender gave it, so the sender sees it arrive.
    const id = item.clientId ?? item.id;
    emit(thread, 'TextMessageStart', { messageId: id, role: 'user' });
    emit(thread, 'TextMessageContent', { messageId: id, delta: userText(item) });
    emit(thread, 'TextMessageEnd', { messageId: id });
    return;
  }
  if (item.type === 'contextCompaction') { emit(thread, 'Custom', { name: 'compaction', value: {} }); return; }
  if (item.type === 'reasoning') {
    const text = reasoningText(item);
    if (!thread.open.has(item.id) && text) {
      emit(thread, 'ReasoningMessageStart', { messageId: item.id });
      emit(thread, 'ReasoningMessageContent', { messageId: item.id, delta: text });
      emit(thread, 'ReasoningMessageEnd', { messageId: item.id });
    } else if (thread.open.delete(item.id)) {
      emit(thread, 'ReasoningMessageEnd', { messageId: item.id });
    }
    return;
  }
  const wasOpen = thread.open.has(item.id);
  if (!wasOpen) itemStarted(thread, item);
  if (!thread.open.has(item.id)) return;
  thread.open.delete(item.id);
  if (item.type === 'agentMessage') {
    // A live message already streamed its text as deltas.
    if (!(live && wasOpen) && item.text) emit(thread, 'TextMessageContent', { messageId: item.id, delta: item.text });
    emit(thread, 'TextMessageEnd', { messageId: item.id });
    return;
  }
  const result = toolResult(item);
  emit(thread, 'ToolCallResult', result);
  const task = thread.tasks.get(item.id);
  if (task?.kind === 'shell') {
    thread.terminals.get(item.id).output = item.aggregatedOutput ?? thread.terminals.get(item.id).output;
    setTask(task, task.stopping ? 'cancelled' : result.status === 'ok' ? 'completed' : 'failed', when);
    publishTasks(thread);
  } else {
    // Only a task's output is served once its call ends.
    thread.terminals.delete(item.id);
  }
  if (live && (item.type === 'collabAgentToolCall' || item.type === 'subAgentActivity')) agentItem(thread, item, when ?? Date.now());
}

// ---- JSON-RPC over the app-server's WebSocket
const ws = new WebSocket(url);
let nextId = 1;
const pending = new Map();
const request = (method, params) => new Promise((resolve, reject) => {
  const id = nextId++;
  pending.set(id, { resolve, reject, method });
  ws.send(JSON.stringify({ id, method, params }));
});
const notify = (method, params) => ws.send(JSON.stringify(params ? { method, params } : { method }));
const say = (s) => process.stdout.write(s);

// No outcome is success; Codex's interrupted turn is AG-UI's cancelled run.
const outcome = (turn) => (turn.status === 'interrupted' ? { outcome: { type: 'cancelled' } } : {});
const TURN_ENDS = { completed: 'completed', interrupted: 'cancelled', failed: 'failed' };

function onNotification(m, p) {
  if (m === 'skills/changed') { publishCommands(true); return; }
  const thread = threads.get(p?.threadId);
  if (!thread) {
    if (!p?.threadId || !main) return;
    // The app-server's other threads are never named, so what they send is bounded.
    if (!early.has(p.threadId)) early.set(p.threadId, []);
    if (early.size > 100) early.delete(early.keys().next().value);
    const waiting = early.get(p.threadId);
    if (waiting && waiting.length < 10000) waiting.push([m, p]);
    return;
  }
  const quiet = thread !== main;
  switch (m) {
    case 'turn/started':
      thread.turnId = thread.runId = p.turn.id; thread.runOpen = true;
      emit(thread, 'RunStarted', { runId: thread.runId, timestamp: Date.now(), ...(thread.parent ? { parent: thread.parent } : {}) }).turn = true;
      setStatus(thread, 'running'); if (!quiet) say('\n[turn started]\n');
      if (thread.parent) {
        const task = threads.get(thread.parent.threadId)?.tasks.get(thread.id);
        if (task && setTask(task, 'running')) publishTasks(threads.get(thread.parent.threadId));
      }
      break;
    case 'turn/completed': {
      // A stopped turn leaves what it waited on unanswered: those are cancelled.
      if (thread.waits.size) resume_(thread, [...thread.waits.keys()].map((interruptId) => ({ interruptId, status: 'cancelled' })), false);
      if (!thread.runOpen) startRun(thread, {});
      emit(thread, 'RunFinished', { runId: thread.runId, ...outcome(p.turn) });
      thread.runOpen = false;
      thread.turnId = null;
      // Its terminals still running were left for later.
      // The rest are forgotten: a call cut short may never complete.
      for (const id of thread.terminals.keys()) {
        backgrounded(thread, id);
        if (!thread.tasks.has(id)) thread.terminals.delete(id);
      }
      if (!quiet) say('\n[turn done]\n> ');
      if (thread.parent) {
        // A subagent's turn ending ends its task, unless it is asked again.
        const parent = threads.get(thread.parent.threadId);
        const task = parent?.tasks.get(thread.id);
        if (task && setTask(task, TURN_ENDS[p.turn.status] ?? 'completed')) publishTasks(parent);
      }
      startQueued(thread);
      break;
    }
    case 'turn/plan/updated': publishChecklist(thread, p.plan ?? []); break;
    case 'item/started':
      itemStarted(thread, p.item, p.startedAtMs);
      if (p.item.type === 'collabAgentToolCall' || p.item.type === 'subAgentActivity') agentItem(thread, p.item, p.startedAtMs ?? Date.now());
      if (p.item.type === 'commandExecution' && !quiet) say(`\n$ ${p.item.command}\n`);
      break;
    case 'item/completed':
      itemCompleted(thread, p.item, true, p.completedAtMs);
      if (quiet) break;
      if (p.item.type === 'userMessage') say(`\n> ${userText(p.item)}\n`);
      if (p.item.type === 'fileChange') say(`\n[edit ${toolTitle(p.item)}]\n`);
      break;
    case 'item/commandExecution/outputDelta': {
      // The result's aggregated output replaces what streamed.
      const command = thread.terminals.get(p.itemId);
      const first = !command?.output;
      // A task keeps only its output's tail.
      if (command) command.output = (command.output + p.delta).slice(-OUTPUT_KEPT);
      emit(thread, 'ToolCallOutput', { toolCallId: p.itemId, delta: p.delta, ...(first ? { mediaType: 'text/x-ansi' } : {}) });
      break;
    }
    case 'item/commandExecution/terminalInteraction': backgrounded(thread, p.itemId); break;
    case 'item/agentMessage/delta': emit(thread, 'TextMessageContent', { messageId: p.itemId, delta: p.delta }); if (!quiet) say(p.delta); break;
    case 'item/reasoning/summaryTextDelta':
      if (!thread.open.has(p.itemId)) { thread.open.add(p.itemId); emit(thread, 'ReasoningMessageStart', { messageId: p.itemId }); }
      emit(thread, 'ReasoningMessageContent', { messageId: p.itemId, delta: p.delta }); break;
    case 'thread/compacted': emit(thread, 'Custom', { name: 'compaction', value: {} }); break;
    case 'thread/name/updated': thread.title = p.threadName ?? null; break;
  }
}

// Codex's decisions for an approval, as the producer offers them (#695): the
// View draws each as a choice. Its cancel, which also stops the turn, stays
// the Stop button.
const DECISIONS = {
  'item/commandExecution/requestApproval': [['accept', 'Approve'], ['acceptForSession', 'Approve for this session'], ['decline', 'Deny']],
  'item/fileChange/requestApproval': [['accept', 'Approve'], ['acceptForSession', 'Approve for this session'], ['decline', 'Deny']],
  execCommandApproval: [['approved', 'Approve'], ['approved_for_session', 'Approve for this session'], ['denied', 'Deny']],
  applyPatchApproval: [['approved', 'Approve'], ['approved_for_session', 'Approve for this session'], ['denied', 'Deny']],
};

function approvalInterrupt(id, method, p) {
  const decision = { title: 'Decision', oneOf: DECISIONS[method].map(([value, title]) => ({ const: value, title })) };
  return { id, reason: 'approval', ...(p.reason ? { message: p.reason } : {}), toolCallId: p.itemId ?? p.callId,
    responseSchema: { type: 'object', required: ['decision'], properties: { decision } } };
}

// A question's options are consts; free text, Codex's "Other", is a string.
function questionInterrupt(id, p) {
  const properties = {};
  for (const q of p.questions) {
    const options = (q.options ?? []).map((o) => ({ const: o.label, title: o.label, description: o.description }));
    const free = q.isOther || !options.length ? [{ type: 'string', title: 'Other' }] : [];
    properties[q.id] = { title: q.header, description: q.question, anyOf: [...options, ...free], ...(q.isSecret ? { writeOnly: true } : {}) };
  }
  return { id, reason: 'question', responseSchema: { type: 'object', required: Object.keys(properties), properties } };
}

function startRun(thread, input) {
  thread.runId = `${thread.turnId}.${++thread.runs}`;
  thread.runOpen = true;
  emit(thread, 'RunStarted', { runId: thread.runId, timestamp: Date.now(), ...(Object.keys(input).length ? { input } : {}),
    ...(thread.parent ? { parent: thread.parent } : {}) });
}

// The run ends on what Codex waits for, every open Interrupt named again.
function interrupt(thread) {
  if (!thread.runOpen) startRun(thread, {});
  emit(thread, 'RunFinished', { runId: thread.runId, outcome: { type: 'interrupt', interrupts: [...thread.waits.values()].map((w) => w.interrupt) } });
  thread.runOpen = false;
  setStatus(thread, 'requires_action');
}

function onServerRequest(msg) {
  const p = msg.params ?? {};
  const id = String(msg.id);
  const thread = threads.get(p.threadId ?? p.conversationId) ?? main;
  if (DECISIONS[msg.method]) {
    thread.waits.set(id, { msgId: msg.id, method: msg.method, interrupt: approvalInterrupt(id, msg.method, p) });
    say(`\n[approval ${id}: ${msg.method}] ${p.command ?? p.reason ?? ''}\n> `);
  } else if (msg.method === 'item/tool/requestUserInput') {
    thread.waits.set(id, { msgId: msg.id, method: msg.method, interrupt: questionInterrupt(id, p) });
    say(`\n[question ${id}] ${p.questions.map((q) => q.question).join(' / ')}\n> `);
  } else {
    ws.send(JSON.stringify({ id: msg.id, error: { code: -32601, message: `agent-host does not handle ${msg.method}` } }));
    return;
  }
  interrupt(thread);
}

// Codex's answer to what it waits on, from a resume entry; null if the entry
// names no choice it offered.
function codexAnswer(waiting, entry) {
  const payload = entry.payload ?? {};
  if (waiting.method === 'item/tool/requestUserInput') {
    if (entry.status === 'cancelled') return { answers: {} };
    const answers = {};
    for (const [qid, value] of Object.entries(payload)) answers[qid] = { answers: [String(value)] };
    return { answers };
  }
  const choices = DECISIONS[waiting.method];
  const decision = entry.status === 'cancelled' ? choices[2][0] : payload.decision;
  return choices.some(([value]) => value === decision) ? { decision } : null;
}

// Answers go to Codex, and a run starts from them, so watchers see them on its
// RunStarted (#690); anything still waiting ends that run again at once.
function resume_(thread, entries, reopen = true) {
  for (const entry of entries) {
    const waiting = thread.waits.get(entry.interruptId);
    thread.waits.delete(entry.interruptId);
    answers.set(entry.interruptId, entry);
    ws.send(JSON.stringify({ id: waiting.msgId, result: codexAnswer(waiting, entry) }));
  }
  startRun(thread, { resume: entries });
  if (thread.waits.size && reopen) interrupt(thread);
  else setStatus(thread, thread.turnId ? 'running' : 'idle');
}

// ---- commands: the app-server has no list, so the bridge offers its own,
// which it maps to app-server calls as codex-acp does, and the skills.
const INIT_PROMPT = 'Generate a file named AGENTS.md that serves as a contributor guide for this repository: '
  + 'its layout, how to build, test and run it, its coding style, and its commit and pull request conventions. '
  + 'Keep it concise and specific to this repository.';
const COMMANDS = [
  { name: 'compact', description: 'Summarize the conversation to free context', call: () => ['thread/compact/start', {}] },
  { name: 'review', description: 'Review the current changes', hint: '[instructions]',
    call: (rest) => ['review/start', { target: rest ? { type: 'custom', instructions: rest } : { type: 'uncommittedChanges' } }] },
  { name: 'review-branch', description: 'Review the changes against a branch', hint: '<branch>',
    call: (rest) => rest && ['review/start', { target: { type: 'baseBranch', branch: rest } }] },
  { name: 'review-commit', description: 'Review the changes a commit made', hint: '<sha>',
    call: (rest) => rest && ['review/start', { target: { type: 'commit', sha: rest, title: null } }] },
  { name: 'init', description: 'Write an AGENTS.md with instructions for Codex', prompt: INIT_PROMPT },
];

async function publishCommands(reload = false) {
  let skills = [];
  try {
    const listed = await request('skills/list', { cwds: [cwd], ...(reload ? { forceReload: true } : {}) });
    skills = listed.data.flatMap((entry) => entry.skills).filter((s) => s.enabled !== false);
  } catch (err) { say(`\n[skills] ${err.message}\n`); }
  publish(main, 'commands', 'application/vnd.flotilla.commands+json', [
    ...COMMANDS.map(({ name, description, hint }) => ({ name, description, ...(hint ? { hint } : {}), insert: `/${name} ` })),
    ...skills.map((s) => ({ name: s.name, description: s.interface?.shortDescription ?? s.shortDescription ?? s.description,
      insert: `$${s.name} `, group: 'Skills' })),
  ]);
}

// A prompt is the last user message of an AG-UI RunAgentInput; the harness
// keeps the history, so only new messages count. While a turn runs, it
// steers that turn.
// `queue` waits for the running turn to end; `steer`, or no mode, joins it.
// A queued prompt starts at once, so the thread doesn't show idle between;
// one that fails to start gives way to the next.
function startQueued(thread) {
  const next = thread.queued.shift();
  if (!next) return setStatus(thread, 'idle');
  prompt(thread, next.text, next.clientId).catch((err) => { say(`\n[error] ${err.message}\n> `); startQueued(thread); });
}

// The bridge's own command texts become app-server calls; any other text,
// slash or not, goes to Codex as typed.
function command(text) {
  const [, name, rest] = /^\/(\S+)\s*([\s\S]*)$/.exec(text.trim()) ?? [];
  const known = COMMANDS.find((c) => c.name === name);
  if (!known) return null;
  if (known.prompt) return { prompt: known.prompt, name, rest };
  const call = known.call(rest.trim());
  return call ? { call, name, rest } : null;
}

async function prompt(thread, text, clientId, mode) {
  if (thread.turnId && mode === 'queue') return thread.queued.push({ text, clientId });
  const mapped = command(text);
  if (mapped) emit(thread, 'Custom', { name: 'command', value: { name: mapped.name, ...(mapped.rest ? { args: mapped.rest } : {}) } });
  if (mapped?.call) {
    const [method, params] = mapped.call;
    return request(method, { threadId: thread.id, ...params });
  }
  const input = [{ type: 'text', text: mapped?.prompt ?? text }];
  if (thread.turnId) return request('turn/steer', { threadId: thread.id, expectedTurnId: thread.turnId, input, clientUserMessageId: clientId });
  return request('turn/start', { threadId: thread.id, input, clientUserMessageId: clientId, ...(approvalPolicy ? { approvalPolicy } : {}) });
}

// The first answer to an approval wins; a second gets 409 naming it.
const answers = new Map();

async function run(thread, input) {
  const resume = input.resume ?? [];
  const message = (input.messages ?? []).filter((m) => m.role === 'user').at(-1);
  const text = typeof message?.content === 'string' ? message.content
    : (message?.content ?? []).filter((c) => c.type === 'text').map((c) => c.text).join('\n');
  // All of it is checked before anything is answered, so a refused request
  // changes nothing.
  if (message ? !text : !resume.length) return [400, { error: 'no user message' }];
  if (message && thread !== main) return [409, { error: 'a subagent takes no direct input' }];
  if (new Set(resume.map((e) => e.interruptId)).size !== resume.length) return [400, { error: 'an interrupt answered twice' }];
  for (const entry of resume) {
    if (answers.has(entry.interruptId)) return [409, { error: 'answered', by: answers.get(entry.interruptId) }];
    const waiting = thread.waits.get(entry.interruptId);
    if (!waiting) return [404, { error: `no interrupt ${entry.interruptId}` }];
    if (!codexAnswer(waiting, entry)) return [400, { error: `no such choice for ${entry.interruptId}` }];
  }
  if (resume.length) resume_(thread, resume);
  if (message) await prompt(thread, text, message.id, input.mode);
  return [202, { accepted: true }];
}

async function stopTask(thread, task) {
  if (task.status !== 'running') return false;
  if (task.kind === 'shell') {
    task.stopping = true;
    const { terminated } = await request('thread/backgroundTerminals/terminate', { threadId: thread.id, processId: task.processId });
    return terminated;
  }
  // A subagent stops as any thread's turn does; Codex has no call to close it.
  const child = threads.get(task.childThreadId);
  if (!child?.turnId) return false;
  await request('turn/interrupt', { threadId: child.id, turnId: child.turnId });
  return true;
}

async function search(query) {
  const q = query.get('q') ?? '';
  const asked = Math.trunc(Number(query.get('limit') ?? 50));
  const limit = Math.min(asked > 0 ? asked : 50, 500);
  if (!q) return [];
  const { files } = await request('fuzzyFileSearch', { query: q, roots: [cwd], cancellationToken: null });
  return files.slice(0, limit).map((f) => ({ path: f.path, kind: f.match_type === 'directory' ? 'dir' : 'file' }));
}

function serve() {
  try { unlinkSync(socket); } catch {}
  // Owner-only from the moment it exists: the socket runs prompts and answers approvals.
  const umask = process.umask(0o177);
  createServer((req, res) => {
    let body = '';
    req.on('data', (chunk) => { body += chunk; });
    req.on('end', async () => {
      const reply = (code, value) => { res.writeHead(code, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(value)); };
      try {
        const { pathname, searchParams } = new URL(req.url, 'http://agent');
        const [, threadsPath, id, verb, taskId, taskVerb] = pathname.split('/').map(decodeURIComponent);
        if (threadsPath !== 'threads') return reply(404, { error: pathname });
        if (id == null || id === '') {
          const listed = await request('thread/list', { limit: 50 }).catch(() => ({ data: [] }));
          const entry = (t) => ({ threadId: t.id, title: t.name ?? t.preview, cwd: t.cwd });
          const named = new Map(listed.data.map((t) => [t.id, entry(t)]));
          // The served threads first, subagents naming the call that spawned them.
          const served = [...threads.values()].map((t) => ({ threadId: t.id, cwd, ...named.get(t.id), ...(t.title ? { title: t.title } : {}),
            ...(t.parent ? { parent: t.parent } : {}) }));
          return reply(200, [...served, ...[...named.values()].filter((t) => !threads.has(t.threadId))]);
        }
        const thread = threads.get(id);
        if (!thread) return reply(404, { error: `no thread ${id}` });
        if (req.method === 'GET' && verb === 'events') {
          if (!searchParams.has('before')) return watch(thread, res, searchParams);
          if (!Number.isFinite(Number(searchParams.get('before')))) return reply(400, { error: 'before takes a seq' });
          return reply(200, page(thread, searchParams));
        }
        if (req.method === 'GET' && verb === 'files') return reply(200, await search(searchParams));
        const task = verb === 'tasks' ? thread.tasks.get(taskId) : undefined;
        if (verb === 'tasks' && !task) return reply(404, { error: `no task ${taskId}` });
        if (req.method === 'GET' && task && taskVerb === 'output') {
          if (!task.output?.url) return reply(404, { error: `task ${taskId} serves no output` });
          res.writeHead(200, { 'Content-Type': 'text/x-ansi; charset=utf-8' });
          return res.end(thread.terminals.get(taskId)?.output ?? '');
        }
        if (req.method !== 'POST') return reply(405, { error: 'GET events, files or a task\'s output, or POST run or stop' });
        if (task && taskVerb === 'stop') return reply(200, { stopped: await stopTask(thread, task) });
        if (verb === 'stop') {
          thread.queued.length = 0;
          // The turn ends, clearing turnId, before the interrupt is answered.
          const stopping = thread.turnId;
          if (stopping) await request('turn/interrupt', { threadId: thread.id, turnId: stopping });
          return reply(200, { stopped: !!stopping });
        }
        if (verb !== 'run') return reply(404, { error: pathname });
        return reply(...(await run(thread, JSON.parse(body || '{}'))));
      } catch (err) {
        return reply(500, { error: err.message });
      }
    });
  }).listen(socket, () => process.umask(umask));
}

// History: pages newest first, emitted oldest first. It is all read here,
// so every event has its `seq` before a watcher pages back through them. The
// subagents it spawned are read after it, as their own threads.
async function history(thread) {
  const turns = [];
  let cursor = null;
  do {
    const page = await request('thread/turns/list', { threadId: thread.id, cursor, limit: 50, sortDirection: 'desc', itemsView: 'full' });
    turns.push(...page.data);
    cursor = page.nextCursor;
  } while (cursor);
  const known = new Set(threads.keys());
  for (const t of turns.reverse()) {
    emit(thread, 'RunStarted', { runId: t.id, ...(thread.parent ? { parent: thread.parent } : {}) }).turn = true;
    for (const item of t.items ?? []) {
      itemCompleted(thread, item, false);
      if (item.type === 'collabAgentToolCall' || item.type === 'subAgentActivity') agentItem(thread, item, item.startedAtMs ?? (t.startedAt ?? 0) * 1000);
    }
    if (t.status === 'inProgress') { thread.turnId = t.id; continue; }
    emit(thread, 'RunFinished', { runId: t.id, ...outcome(t) });
  }
  setStatus(thread, thread.turnId ? 'running' : 'idle');
  for (const child of [...threads.values()].filter((c) => !known.has(c.id))) {
    await history(child).catch((err) => say(`\n[history of ${child.id}] ${err.message}\n`));
  }
  return turns.length;
}

ws.addEventListener('message', (e) => {
  const msg = JSON.parse(e.data);
  if (msg.id != null && msg.method) return onServerRequest(msg);
  if (msg.id != null) {
    const r = pending.get(msg.id); pending.delete(msg.id);
    if (!r) return;
    return msg.error ? r.reject(new Error(`${r.method}: ${JSON.stringify(msg.error)}`)) : r.resolve(msg.result);
  }
  onNotification(msg.method, msg.params);
});
ws.addEventListener('error', (e) => { console.error('websocket error', e.message ?? e); process.exit(1); });
ws.addEventListener('close', () => { say('\n[app-server closed]\n'); process.exit(0); });

ws.addEventListener('open', async () => {
  // Background terminals are stopped through an experimental method.
  await request('initialize', { clientInfo: { name: 'wheelhouse-agent-host', version: '0.0.1' }, capabilities: { experimentalApi: true } });
  notify('initialized');
  let turns = 0;
  if (!opt('--thread')) {
    const r = await request('thread/start', { cwd });
    main = addThread(r.thread.id, null);
    setStatus(main, 'idle');
  } else {
    await request('thread/resume', { threadId: opt('--thread'), excludeTurns: true });
    main = addThread(opt('--thread'), null);
    // A new thread has no history until its first prompt.
    turns = await history(main);
  }
  await publishCommands();
  serve();
  say(`thread ${main.id}: ${turns} turns of history, ${main.log.length} events, on ${socket}\n> `);
  const rl = createInterface({ input: process.stdin });
  rl.on('line', async (line) => {
    const text = line.trim();
    if (!text) return;
    try {
      const approval = [...main.waits.entries()].find(([, w]) => DECISIONS[w.method]);
      if (approval && /^[yn]/i.test(text)) {
        const choices = DECISIONS[approval[1].method];
        resume_(main, [{ interruptId: approval[0], status: 'resolved', payload: { decision: choices[/^y/i.test(text) ? 0 : 2][0] } }]);
      } else if (text === '/interrupt') {
        if (main.turnId) await request('turn/interrupt', { threadId: main.id, turnId: main.turnId });
      } else {
        await prompt(main, text);
      }
    } catch (err) { say(`\n[error] ${err.message}\n> `); }
  });
});
