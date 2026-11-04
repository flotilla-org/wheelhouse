#!/usr/bin/env node
// Prototype (#683): a Codex app-server client that writes Wheelhouse's conversation
// event contract (#548) to a file the rich_text View follows, and takes
// prompts on stdin. Throwaway; it exists to learn what an agent View needs.
//
//   node tools/codex-agent-host.mjs --url ws://127.0.0.1:PORT (--new [--cwd DIR] | --thread ID) -o FILE [--watch] [--input SOCKET]
//
// stdin: a line is a prompt (turn/start, or turn/steer while a turn runs);
// /interrupt stops the turn; y or n answers a pending approval. With
// --input, the same over HTTP on a unix socket (POST /run, POST /stop), and
// the hello names it so a View can show a composer.
import { appendFileSync, unlinkSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { createInterface } from 'node:readline';

const args = process.argv.slice(2);
const opt = (name) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : undefined; };
const url = opt('--url') ?? 'ws://127.0.0.1:4599';
const out = opt('-o') ?? 'codex.events.jsonl';
const cwd = opt('--cwd') ?? process.cwd();
const watchOnly = args.includes('--watch');
let threadId = opt('--thread');

// ---- contract output
let seq = 0;
const write = (ev) => appendFileSync(out, JSON.stringify(ev) + '\n');
const emit = (type, fields) => write({ type, threadId, seq: ++seq, ...fields });

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
  const base = { toolCallId: item.id, messageId: `${item.id}-result`, status };
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
  } else if (item.type === 'webSearch' && !toolTitle(item)) {
    // The query arrives only when the search completes.
  } else if (KINDS[item.type]) {
    open.add(item.id);
    emit('ToolCallStart', { toolCallId: item.id, toolCallName: item.type, title: toolTitle(item), kind: KINDS[item.type] });
    const input = item.type === 'commandExecution' ? { command: item.command, cwd: item.cwd } : item.arguments ?? {};
    emit('ToolCallArgs', { toolCallId: item.id, delta: JSON.stringify(input) });
    emit('ToolCallEnd', { toolCallId: item.id });
  }
}
function itemCompleted(item, live) {
  if (item.type === 'userMessage') {
    const text = userText(item);
    emit('TextMessageStart', { messageId: item.id, role: 'user' });
    emit('TextMessageContent', { messageId: item.id, delta: text });
    emit('TextMessageEnd', { messageId: item.id });
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

function onNotification(m, p) {
  if (p?.threadId && p.threadId !== threadId) return;
  switch (m) {
    case 'turn/started':
      turnId = p.turn.id; emit('RunStarted', { runId: p.turn.id }); say('\n[turn started]\n'); break;
    case 'turn/completed':
      emit('RunFinished', { runId: p.turn.id, outcome: p.turn.status === 'interrupted' ? 'interrupted' : 'completed' });
      turnId = null; say('\n[turn done]\n> '); break;
    case 'item/started': itemStarted(p.item); if (p.item.type === 'commandExecution') say(`\n$ ${p.item.command}\n`); break;
    case 'item/completed':
      itemCompleted(p.item, true);
      if (p.item.type === 'userMessage') say(`\n> ${userText(p.item)}\n`);
      if (p.item.type === 'fileChange') say(`\n[edit ${toolTitle(p.item)}]\n`);
      break;
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
    // The chat shows the request as a Flotilla-style Custom event until the contract names one.
    emit('Custom', { name: 'approval', value: { method: msg.method, itemId: p.itemId, what } });
    say(`\n[approval: ${msg.method}] ${what ?? ''}\napprove? y/n > `);
  } else {
    ws.send(JSON.stringify({ id: msg.id, error: { code: -32601, message: `agent-host does not handle ${msg.method}` } }));
  }
}

// A prompt is the last user message of an AG-UI RunAgentInput; the harness
// keeps the history, so only new messages count. While a turn runs, it
// steers that turn.
async function prompt(text, clientId) {
  const input = [{ type: 'text', text }];
  if (turnId) return request('turn/steer', { threadId, expectedTurnId: turnId, input, clientUserMessageId: clientId });
  return request('turn/start', { threadId, input, clientUserMessageId: clientId });
}

// Input over HTTP on a unix socket: POST /run takes a RunAgentInput, POST
// /stop interrupts the running turn.
function serveInput(socket) {
  try { unlinkSync(socket); } catch {}
  createServer((req, res) => {
    let body = '';
    req.on('data', (chunk) => { body += chunk; });
    req.on('end', async () => {
      const reply = (code, value) => { res.writeHead(code, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(value)); };
      try {
        if (req.method !== 'POST') return reply(405, { error: 'POST only' });
        if (req.url === '/stop') {
          if (turnId) await request('turn/interrupt', { threadId, turnId });
          return reply(200, { stopped: !!turnId });
        }
        if (req.url !== '/run') return reply(404, { error: req.url });
        const run = JSON.parse(body || '{}');
        const message = (run.messages ?? []).filter((m) => m.role === 'user').at(-1);
        const text = typeof message?.content === 'string' ? message.content
          : (message?.content ?? []).filter((c) => c.type === 'text').map((c) => c.text).join('\n');
        if (!text) return reply(400, { error: 'no user message' });
        await prompt(text, message.id);
        return reply(202, { accepted: true });
      } catch (err) {
        return reply(500, { error: err.message });
      }
    });
  }).listen(socket);
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
  const socket = opt('--input');
  if (socket) serveInput(socket);
  writeFileSync(out, JSON.stringify({ type: 'Hello', contract: 1, ...(socket ? { input: `unix://${socket}` } : {}) }) + '\n');
  // History: pages newest first, emitted oldest first. The file can't take a
  // page in front of what it holds, so the whole history is read here.
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
    emit('RunFinished', { runId: t.id, outcome: t.status === 'interrupted' ? 'interrupted' : 'completed' });
  }
  say(`thread ${threadId}: ${turns.length} turns of history -> ${out}\n> `);
  if (watchOnly) return;
  const rl = createInterface({ input: process.stdin });
  rl.on('line', async (line) => {
    const text = line.trim();
    if (!text) return;
    try {
      if (approval) {
        const yes = /^y/i.test(text);
        const decision = approval.method.startsWith('item/') ? (yes ? 'accept' : 'decline') : (yes ? 'approved' : 'denied');
        ws.send(JSON.stringify({ id: approval.id, result: { decision } }));
        approval = null;
      } else if (text === '/interrupt') {
        if (turnId) await request('turn/interrupt', { threadId, turnId });
      } else {
        await prompt(text);
      }
    } catch (err) { say(`\n[error] ${err.message}\n> `); }
  });
});
