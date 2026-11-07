#!/usr/bin/env node
// Prototype (#683): a Codex app-server client that serves one thread over the Agent
// View protocol (#691): Wheelhouse's conversation contract (#548, version 2) as a watch
// stream on a unix socket, and prompts in. Throwaway; Flotilla's connectors own live
// producers.
//
//   node tools/codex-agent-host.mjs --url ws://127.0.0.1:PORT (--new [--cwd DIR] | --thread ID) --socket PATH [--buffer N] [--page BYTES]
//     [--approval untrusted|on-request|never]
//
// GET /threads, GET /threads/{id}/events (?after=N, ?before=N&limit=M), POST
// /threads/{id}/run and /stop. stdin still works: a line is a prompt;
// /interrupt stops the turn; y or n answers a pending approval.
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
let threadId = opt('--thread');
// Codex's approval policy for the turns it starts: untrusted, on-request or never.
const approvalPolicy = opt('--approval');

// ---- the thread's events, each numbered by its place in the stream. Numbers
// start past any an earlier bridge gave out, so a watcher resuming from one is
// sent a snapshot rather than this log's events at its numbers.
const base = Date.now() * 1024;
const log = [];
const watchers = new Set();
const emit = (type, fields) => {
  const event = { type, threadId, seq: base + log.length + 1, ...fields };
  const line = JSON.stringify(event) + '\n';
  const record = { event, line };
  log.push(record);
  for (const res of watchers) res.write(line);
  return record;
};

// Items a snapshot's earlier count and a page's limit count, as Wheelhouse does.
const STARTS = new Set(['TextMessageStart', 'ToolCallStart', 'ReasoningMessageStart', 'Custom']);
const itemsIn = (from, to) => log.slice(from, to).filter(({ event }) => STARTS.has(event.type)).length;
// Snapshots and pages cut only where a turn starts, so an open turn and its
// open items always come whole; the runs an Interrupt cuts it into don't count.
const runStart = (i) => { while (i > 0 && !log[i].turn) i--; return i; };

// Published once the history is read, then on every change.
let status = null;
const setStatus = (value) => {
  if (value === status) return;
  status = value;
  emit('StateSnapshot', { key: 'status', mediaType: 'application/json', snapshot: value });
};

const hello = (snapshot) => ({ type: 'Hello', contract: 2, threadId,
  capabilities: { input: true, modes: ['steer', 'queue'], commands: false, roots: [{ path: cwd, local: true }] },
  ...(snapshot ? { snapshot } : {}) });

function watch(res, query) {
  res.writeHead(200, { 'Content-Type': 'application/jsonl', 'Cache-Control': 'no-store' });
  const after = query.has('after') ? Number(query.get('after')) - base : NaN;
  const write = (event) => res.write(JSON.stringify(event) + '\n');
  if (after >= Math.max(0, log.length - buffer) && after <= log.length) {
    write(hello());
    for (const { line } of log.slice(after)) res.write(line);
  } else {
    let start = log.length, bytes = 0;
    while (start > 0 && bytes < pageBytes) bytes += log[--start].line.length;
    start = runStart(start);
    write(hello({ earlierItems: itemsIn(0, start), oldestSeq: base + start + 1 }));
    const restated = log.slice(0, start).findLast(({ event }) => event.type === 'StateSnapshot' && event.key === 'status');
    if (restated) res.write(restated.line);
    for (const { line } of log.slice(start)) res.write(line);
  }
  watchers.add(res);
  // The response's close, not the request's: that fires once its body is read.
  res.on('close', () => watchers.delete(res));
}

function page(query) {
  const before = Math.max(0, Math.min(Number(query.get('before') ?? base + log.length + 1) - base - 1, log.length));
  const asked = Math.trunc(Number(query.get('limit') ?? 50));
  const limit = Math.min(asked > 0 ? asked : 50, 500);
  let start = before;
  while (start > 0 && itemsIn(start, before) < limit) start--;
  start = runStart(start);
  return { events: log.slice(start, before).map(({ event }) => event), earlierItems: itemsIn(0, start), oldestSeq: base + start + 1 };
}

const KINDS = { commandExecution: 'shell', fileChange: 'edit', mcpToolCall: 'mcp', dynamicToolCall: 'other',
  webSearch: 'web', collabAgentToolCall: 'agent', imageView: 'read' };
const userText = (item) => (item.content ?? []).filter((c) => c.type === 'text').map((c) => c.text).join('\n');
const reasoningText = (item) => [...(item.summary ?? []), ...(item.summary?.length ? [] : item.content ?? [])]
  .map((s) => (typeof s === 'string' ? s : s.text ?? '')).join('\n\n');

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
  const status = failed ? 'failed' : 'ok';
  const base = { toolCallId: item.id, messageId: `${item.id}-result`, status,
    ...(item.type === 'webSearch' ? { title: toolTitle(item) || undefined } : {}) };
  switch (item.type) {
    case 'commandExecution': return { ...base, mediaType: 'text/x-ansi', content: item.aggregatedOutput ?? '' };
    case 'fileChange': return { ...base, mediaType: 'text/x-diff', content: diffOf(item) };
    case 'mcpToolCall': return { ...base, mediaType: 'text/plain', content: JSON.stringify(item.error ?? item.result ?? null, null, 2) };
    case 'webSearch': return { ...base, mediaType: 'text/markdown', content: (item.results ?? []).map((r) =>
      `- [${(r.title || r.url).replace(/[[\]]/g, '')}](${r.url}) · ${r.domain ?? ''}${r.snippet ? `\n  ${r.snippet.replace(/\s+/g, ' ')}` : ''}`).join('\n') };
    default: return { ...base, mediaType: 'text/plain', content: JSON.stringify(item.contentItems ?? item.results ?? '', null, 2) };
  }
}

// Live items open on item/started, grow by deltas and close on item/completed;
// history items arrive closed. Both go through these two.
const open = new Set();
function itemStarted(item) {
  if (open.has(item.id)) return;
  if (item.type === 'agentMessage') {
    open.add(item.id); emit('TextMessageStart', { messageId: item.id, role: 'assistant' });
  } else if (item.type === 'reasoning') {
    // Reasoning opens on its first text: Codex often sends none, and an empty
    // Block would show as a gap.
  } else if (KINDS[item.type]) {
    open.add(item.id);
    // A web search's query arrives only when it completes, as a later title.
    emit('ToolCallStart', { toolCallId: item.id, toolCallName: item.type, title: toolTitle(item) || undefined, kind: KINDS[item.type] });
    const input = item.type === 'commandExecution' ? { command: item.command, cwd: item.cwd } : item.arguments ?? {};
    emit('ToolCallArgs', { toolCallId: item.id, delta: JSON.stringify(input) });
    emit('ToolCallEnd', { toolCallId: item.id });
  }
}
function itemCompleted(item, live) {
  if (item.type === 'userMessage') {
    // A prompt keeps the id its sender gave it, so the sender sees it arrive.
    const id = item.clientId ?? item.id;
    emit('TextMessageStart', { messageId: id, role: 'user' });
    emit('TextMessageContent', { messageId: id, delta: userText(item) });
    emit('TextMessageEnd', { messageId: id });
    return;
  }
  if (item.type === 'contextCompaction') { emit('Custom', { name: 'compaction', value: {} }); return; }
  if (item.type === 'reasoning') {
    const text = reasoningText(item);
    if (!open.has(item.id) && text) {
      emit('ReasoningMessageStart', { messageId: item.id });
      emit('ReasoningMessageContent', { messageId: item.id, delta: text });
      emit('ReasoningMessageEnd', { messageId: item.id });
    } else if (open.delete(item.id)) {
      emit('ReasoningMessageEnd', { messageId: item.id });
    }
    return;
  }
  const wasOpen = open.has(item.id);
  if (!wasOpen) itemStarted(item);
  if (!open.has(item.id)) return;
  open.delete(item.id);
  if (item.type === 'agentMessage') {
    // A live message already streamed its text as deltas.
    if (!(live && wasOpen) && item.text) emit('TextMessageContent', { messageId: item.id, delta: item.text });
    emit('TextMessageEnd', { messageId: item.id });
  } else {
    emit('ToolCallResult', toolResult(item));
  }
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

let turnId = null;
// The AG-UI run: Codex's turn, cut into runs where it waits on an Interrupt.
let runId = null;
let runOpen = false;
let runs = 0;
// What Codex waits on, by Interrupt id: {msgId, method, interrupt}.
const waits = new Map();
const streamed = new Set();
// No outcome is success; Codex's interrupted turn is AG-UI's cancelled run.
const outcome = (turn) => (turn.status === 'interrupted' ? { outcome: { type: 'cancelled' } } : {});

function onNotification(m, p) {
  if (p?.threadId && p.threadId !== threadId) return;
  switch (m) {
    case 'turn/started':
      turnId = runId = p.turn.id; runOpen = true;
      emit('RunStarted', { runId }).turn = true; setStatus('running'); say('\n[turn started]\n'); break;
    case 'turn/completed': {
      // A stopped turn leaves what it waited on unanswered: those are cancelled.
      if (waits.size) resume_([...waits.keys()].map((interruptId) => ({ interruptId, status: 'cancelled' })), false);
      if (!runOpen) startRun({});
      emit('RunFinished', { runId, ...outcome(p.turn) });
      runOpen = false;
      turnId = null; say('\n[turn done]\n> ');
      startQueued();
      break;
    }
    case 'item/started': itemStarted(p.item); if (p.item.type === 'commandExecution') say(`\n$ ${p.item.command}\n`); break;
    case 'item/completed':
      itemCompleted(p.item, true);
      if (p.item.type === 'userMessage') say(`\n> ${userText(p.item)}\n`);
      if (p.item.type === 'fileChange') say(`\n[edit ${toolTitle(p.item)}]\n`);
      break;
    case 'item/commandExecution/outputDelta': {
      // The result's aggregated output replaces what streamed.
      const first = !streamed.has(p.itemId);
      streamed.add(p.itemId);
      emit('ToolCallOutput', { toolCallId: p.itemId, delta: p.delta, ...(first ? { mediaType: 'text/x-ansi' } : {}) });
      break;
    }
    case 'item/agentMessage/delta': emit('TextMessageContent', { messageId: p.itemId, delta: p.delta }); say(p.delta); break;
    case 'item/reasoning/summaryTextDelta':
      if (!open.has(p.itemId)) { open.add(p.itemId); emit('ReasoningMessageStart', { messageId: p.itemId }); }
      emit('ReasoningMessageContent', { messageId: p.itemId, delta: p.delta }); break;
    case 'thread/compacted': emit('Custom', { name: 'compaction', value: {} }); break;
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

function startRun(input) {
  runId = `${turnId}.${++runs}`;
  runOpen = true;
  emit('RunStarted', { runId, ...(Object.keys(input).length ? { input } : {}) });
}

// The run ends on what Codex waits for, every open Interrupt named again.
function interrupt() {
  if (!runOpen) startRun({});
  emit('RunFinished', { runId, outcome: { type: 'interrupt', interrupts: [...waits.values()].map((w) => w.interrupt) } });
  runOpen = false;
  setStatus('requires_action');
}

function onServerRequest(msg) {
  const p = msg.params ?? {};
  const id = String(msg.id);
  if (DECISIONS[msg.method]) {
    waits.set(id, { msgId: msg.id, method: msg.method, interrupt: approvalInterrupt(id, msg.method, p) });
    say(`\n[approval ${id}: ${msg.method}] ${p.command ?? p.reason ?? ''}\n> `);
  } else if (msg.method === 'item/tool/requestUserInput') {
    waits.set(id, { msgId: msg.id, method: msg.method, interrupt: questionInterrupt(id, p) });
    say(`\n[question ${id}] ${p.questions.map((q) => q.question).join(' / ')}\n> `);
  } else {
    ws.send(JSON.stringify({ id: msg.id, error: { code: -32601, message: `agent-host does not handle ${msg.method}` } }));
    return;
  }
  interrupt();
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
function resume_(entries, reopen = true) {
  for (const entry of entries) {
    const waiting = waits.get(entry.interruptId);
    waits.delete(entry.interruptId);
    answers.set(entry.interruptId, entry);
    ws.send(JSON.stringify({ id: waiting.msgId, result: codexAnswer(waiting, entry) }));
  }
  startRun({ resume: entries });
  if (waits.size && reopen) interrupt();
  else setStatus(turnId ? 'running' : 'idle');
}

// A prompt is the last user message of an AG-UI RunAgentInput; the harness
// keeps the history, so only new messages count. While a turn runs, it
// steers that turn.
// `queue` waits for the running turn to end; `steer`, or no mode, joins it.
const queued = [];
// A queued prompt starts at once, so the thread doesn't show idle between;
// one that fails to start gives way to the next.
function startQueued() {
  const next = queued.shift();
  if (!next) return setStatus('idle');
  prompt(next.text, next.clientId).catch((err) => { say(`\n[error] ${err.message}\n> `); startQueued(); });
}

async function prompt(text, clientId, mode) {
  const input = [{ type: 'text', text }];
  if (turnId && mode === 'queue') return queued.push({ text, clientId });
  if (turnId) return request('turn/steer', { threadId, expectedTurnId: turnId, input, clientUserMessageId: clientId });
  return request('turn/start', { threadId, input, clientUserMessageId: clientId, ...(approvalPolicy ? { approvalPolicy } : {}) });
}

// The first answer to an approval wins; a second gets 409 naming it.
const answers = new Map();

async function run(input) {
  const resume = input.resume ?? [];
  const message = (input.messages ?? []).filter((m) => m.role === 'user').at(-1);
  const text = typeof message?.content === 'string' ? message.content
    : (message?.content ?? []).filter((c) => c.type === 'text').map((c) => c.text).join('\n');
  // All of it is checked before anything is answered, so a refused request
  // changes nothing.
  if (message ? !text : !resume.length) return [400, { error: 'no user message' }];
  for (const entry of resume) {
    if (answers.has(entry.interruptId)) return [409, { error: 'answered', by: answers.get(entry.interruptId) }];
    const waiting = waits.get(entry.interruptId);
    if (!waiting) return [404, { error: `no interrupt ${entry.interruptId}` }];
    if (!codexAnswer(waiting, entry)) return [400, { error: `no such choice for ${entry.interruptId}` }];
  }
  if (resume.length) resume_(resume);
  if (message) await prompt(text, message.id, input.mode);
  return [202, { accepted: true }];
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
        const [, threads, id, verb] = pathname.split('/').map(decodeURIComponent);
        if (threads !== 'threads') return reply(404, { error: pathname });
        if (id == null || id === '') {
          const listed = await request('thread/list', { limit: 50 }).catch(() => ({ data: [] }));
          const all = listed.data.map((t) => ({ threadId: t.id, title: t.name ?? t.preview, cwd: t.cwd }));
          if (!all.some((t) => t.threadId === threadId)) all.unshift({ threadId, cwd });
          return reply(200, all);
        }
        if (id !== threadId) return reply(404, { error: `no thread ${id}` });
        if (req.method === 'GET' && verb === 'events') {
          if (!searchParams.has('before')) return watch(res, searchParams);
          if (!Number.isFinite(Number(searchParams.get('before')))) return reply(400, { error: 'before takes a seq' });
          return reply(200, page(searchParams));
        }
        if (req.method !== 'POST') return reply(405, { error: 'GET events, or POST run or stop' });
        if (verb === 'stop') {
          queued.length = 0;
          // The turn ends, clearing turnId, before the interrupt is answered.
          const stopping = turnId;
          if (stopping) await request('turn/interrupt', { threadId, turnId: stopping });
          return reply(200, { stopped: !!stopping });
        }
        if (verb !== 'run') return reply(404, { error: pathname });
        return reply(...(await run(JSON.parse(body || '{}'))));
      } catch (err) {
        return reply(500, { error: err.message });
      }
    });
  }).listen(socket, () => process.umask(umask));
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
  await request('initialize', { clientInfo: { name: 'wheelhouse-agent-host', version: '0.0.1' } });
  notify('initialized');
  if (!threadId) {
    const r = await request('thread/start', { cwd });
    threadId = r.thread.id;
  } else {
    await request('thread/resume', { threadId, excludeTurns: true });
  }
  // History: pages newest first, emitted oldest first. It is all read here,
  // so every event has its `seq` before a watcher pages back through them.
  const turns = [];
  let cursor = null;
  // A new thread has no history until its first prompt.
  if (opt('--thread')) do {
    const page = await request('thread/turns/list', { threadId, cursor, limit: 50, sortDirection: 'desc', itemsView: 'full' });
    turns.push(...page.data);
    cursor = page.nextCursor;
  } while (cursor);
  for (const t of turns.reverse()) {
    emit('RunStarted', { runId: t.id }).turn = true;
    for (const item of t.items ?? []) itemCompleted(item, false);
    if (t.status === 'inProgress') { turnId = t.id; continue; }
    emit('RunFinished', { runId: t.id, ...outcome(t) });
  }
  setStatus(turnId ? 'running' : 'idle');
  serve();
  say(`thread ${threadId}: ${turns.length} turns of history, ${log.length} events, on ${socket}\n> `);
  const rl = createInterface({ input: process.stdin });
  rl.on('line', async (line) => {
    const text = line.trim();
    if (!text) return;
    try {
      const approval = [...waits.entries()].find(([, w]) => DECISIONS[w.method]);
      if (approval && /^[yn]/i.test(text)) {
        const choices = DECISIONS[approval[1].method];
        resume_([{ interruptId: approval[0], status: 'resolved', payload: { decision: choices[/^y/i.test(text) ? 0 : 2][0] } }]);
      } else if (text === '/interrupt') {
        if (turnId) await request('turn/interrupt', { threadId, turnId });
      } else {
        await prompt(text);
      }
    } catch (err) { say(`\n[error] ${err.message}\n> `); }
  });
});
