#!/usr/bin/env node
// Prototype (#683): a Codex app-server client that serves one thread over the Agent
// View protocol (#691): Wheelhouse's conversation contract (#548, version 2) as a watch
// stream on a unix socket, and prompts in. Throwaway; Flotilla's connectors own live
// producers.
//
//   node tools/codex-agent-host.mjs --url ws://127.0.0.1:PORT (--new [--cwd DIR] | --thread ID) --socket PATH [--buffer N] [--page BYTES]
//
// GET /threads, GET /threads/{id}/events (?after=N, ?before=N&limit=M), POST
// /threads/{id}/run and /stop. stdin still works: a line is a prompt;
// /interrupt stops the turn; y or n answers a pending approval.
import { chmodSync, unlinkSync } from 'node:fs';
import { createServer } from 'node:http';
import { createInterface } from 'node:readline';

const args = process.argv.slice(2);
const opt = (name) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : undefined; };
const url = opt('--url') ?? 'ws://127.0.0.1:4599';
const socket = opt('--socket') ?? 'codex-agent.sock';
const cwd = opt('--cwd') ?? process.cwd();
// How far back `?after=` can resume; older starts over with a snapshot.
const buffer = Number(opt('--buffer') ?? 100000);
// A snapshot's newest page, at least this many bytes of whole runs.
const pageBytes = Number(opt('--page') ?? 256 * 1024);
let threadId = opt('--thread');

// ---- the thread's events, each numbered by its place in the stream. Numbers
// start past any an earlier bridge gave out, so a watcher resuming from one is
// sent a snapshot rather than this log's events at its numbers.
const base = Date.now() * 1024;
const log = [];
const watchers = new Set();
const emit = (type, fields) => {
  const event = { type, threadId, seq: base + log.length + 1, ...fields };
  const line = JSON.stringify(event) + '\n';
  log.push({ event, line });
  for (const res of watchers) res.write(line);
};

// Items a snapshot's earlier count and a page's limit count, as Wheelhouse does.
const STARTS = new Set(['TextMessageStart', 'ToolCallStart', 'ReasoningMessageStart', 'Custom']);
const itemsIn = (from, to) => log.slice(from, to).filter(({ event }) => STARTS.has(event.type)).length;
// Snapshots and pages cut only where a run starts, so an open run and its
// open items always come whole.
const runStart = (i) => { while (i > 0 && log[i].event.type !== 'RunStarted') i--; return i; };

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
let approval = null; // {id, kind}
const streamed = new Set();
// No outcome is success; Codex's interrupted turn is AG-UI's cancelled run.
const outcome = (turn) => (turn.status === 'interrupted' ? { outcome: { type: 'cancelled' } } : {});

function onNotification(m, p) {
  if (p?.threadId && p.threadId !== threadId) return;
  switch (m) {
    case 'turn/started':
      turnId = p.turn.id; emit('RunStarted', { runId: p.turn.id }); setStatus('running'); say('\n[turn started]\n'); break;
    case 'turn/completed': {
      emit('RunFinished', { runId: p.turn.id, ...outcome(p.turn) });
      turnId = null; setStatus('idle'); say('\n[turn done]\n> ');
      const next = queued.shift();
      if (next) prompt(next.text, next.clientId).catch((err) => say(`\n[error] ${err.message}\n> `));
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

function onServerRequest(msg) {
  const p = msg.params ?? {};
  if (msg.method.endsWith('requestApproval') || msg.method === 'applyPatchApproval' || msg.method === 'execCommandApproval') {
    approval = { id: msg.id, method: msg.method };
    const what = p.command ?? p.reason ?? p.itemId;
    // The chat shows the request as a Flotilla-style Custom event until #695 draws an
    // Interrupt; a watcher answers it by this id in a resume entry.
    emit('Custom', { name: 'approval', value: { interruptId: String(msg.id), method: msg.method, itemId: p.itemId, what } });
    setStatus('requires_action');
    say(`\n[approval: ${msg.method}] ${what ?? ''}\napprove? y/n > `);
  } else {
    ws.send(JSON.stringify({ id: msg.id, error: { code: -32601, message: `agent-host does not handle ${msg.method}` } }));
  }
}

function answer(yes) {
  const decision = approval.method.startsWith('item/') ? (yes ? 'accept' : 'decline') : (yes ? 'approved' : 'denied');
  ws.send(JSON.stringify({ id: approval.id, result: { decision } }));
  approval = null;
  setStatus(turnId ? 'running' : 'idle');
}

// A prompt is the last user message of an AG-UI RunAgentInput; the harness
// keeps the history, so only new messages count. While a turn runs, it
// steers that turn.
// `queue` waits for the running turn to end; `steer`, or no mode, joins it.
const queued = [];
async function prompt(text, clientId, mode) {
  const input = [{ type: 'text', text }];
  if (turnId && mode === 'queue') return queued.push({ text, clientId });
  if (turnId) return request('turn/steer', { threadId, expectedTurnId: turnId, input, clientUserMessageId: clientId });
  return request('turn/start', { threadId, input, clientUserMessageId: clientId });
}

// The first answer to an approval wins; a second gets 409 naming it.
const answers = new Map();

async function run(input) {
  for (const entry of input.resume ?? []) {
    if (answers.has(entry.interruptId)) return [409, { error: 'answered', by: answers.get(entry.interruptId) }];
    if (!approval || String(approval.id) !== entry.interruptId) return [404, { error: `no interrupt ${entry.interruptId}` }];
    answers.set(entry.interruptId, entry);
    answer(entry.status === 'resolved' && entry.payload?.approve !== false);
  }
  const message = (input.messages ?? []).filter((m) => m.role === 'user').at(-1);
  if (!message) return input.resume?.length ? [202, { accepted: true }] : [400, { error: 'no user message' }];
  const text = typeof message.content === 'string' ? message.content
    : (message.content ?? []).filter((c) => c.type === 'text').map((c) => c.text).join('\n');
  if (!text) return [400, { error: 'no user message' }];
  await prompt(text, message.id, input.mode);
  return [202, { accepted: true }];
}

function serve() {
  try { unlinkSync(socket); } catch {}
  createServer((req, res) => {
    let body = '';
    req.on('data', (chunk) => { body += chunk; });
    req.on('end', async () => {
      const reply = (code, value) => { res.writeHead(code, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(value)); };
      const { pathname, searchParams } = new URL(req.url, 'http://agent');
      const [, threads, id, verb] = pathname.split('/').map(decodeURIComponent);
      try {
        if (threads !== 'threads') return reply(404, { error: pathname });
        if (id == null || id === '') {
          const listed = await request('thread/list', { limit: 50 }).catch(() => ({ data: [] }));
          const all = listed.data.map((t) => ({ threadId: t.id, title: t.name ?? t.preview, cwd: t.cwd }));
          if (!all.some((t) => t.threadId === threadId)) all.unshift({ threadId, cwd });
          return reply(200, all);
        }
        if (id !== threadId) return reply(404, { error: `no thread ${id}` });
        if (req.method === 'GET' && verb === 'events') {
          return searchParams.has('before') ? reply(200, page(searchParams)) : watch(res, searchParams);
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
  }).listen(socket, () => chmodSync(socket, 0o600));
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
    emit('RunStarted', { runId: t.id });
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
      if (approval) {
        answers.set(String(approval.id), { status: 'resolved', from: 'stdin' });
        answer(/^y/i.test(text));
      } else if (text === '/interrupt') {
        if (turnId) await request('turn/interrupt', { threadId, turnId });
      } else {
        await prompt(text);
      }
    } catch (err) { say(`\n[error] ${err.message}\n> `); }
  });
});
