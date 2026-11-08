#!/usr/bin/env node
// Prototype (#696): a Claude Agent SDK session served over the Agent View
// protocol (#691), as tools/codex-agent-host.mjs serves a Codex thread. Kept
// to show the protocol holds for a second harness; Flotilla's connectors own
// live producers.
//
//   npm install --prefix tools/claude-agent-host
//   node tools/claude-agent-host/claude-agent-host.mjs (--new [--cwd DIR] | --session ID) --socket PATH
//     [--permission-mode default|acceptEdits|plan|...] [--settings user,project,local] [--model M]
//     [--buffer N] [--page BYTES] [--trace FILE]
//
// Routes as the Codex bridge, plus POST /threads/{id}/tasks/{taskId}/stop.
// stdin: a line is a prompt; /interrupt stops the turn; y or n answers an approval.
import { getSessionInfo, listSessions, query } from '@anthropic-ai/claude-agent-sdk';
import { execFileSync } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import { appendFileSync, existsSync, readdirSync, unlinkSync } from 'node:fs';
import { createServer } from 'node:http';
import { homedir } from 'node:os';
import { join } from 'node:path';
import { createInterface } from 'node:readline';
import { fileURLToPath } from 'node:url';

const args = process.argv.slice(2);
const opt = (name) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : undefined; };
const count = (name, fallback) => {
  const n = Number(opt(name) ?? fallback);
  if (!(n > 0)) throw new Error(`${name} takes a positive number`);
  return n;
};
const socket = opt('--socket') ?? 'claude-agent.sock';
let cwd = opt('--cwd') ?? process.cwd();
const buffer = count('--buffer', 100000);
const pageBytes = count('--page', 256 * 1024);
const permissionMode = opt('--permission-mode') ?? 'default';
// No settings by default, so a developer's allow rules don't answer the
// approvals this is here to show.
const settingSources = (opt('--settings') ?? '').split(',').filter(Boolean);
const resumed = opt('--session');
const threadId = resumed ?? randomUUID();

// ---- the thread's events, numbered as the Codex bridge numbers them.
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

const STARTS = new Set(['TextMessageStart', 'ToolCallStart', 'ReasoningMessageStart', 'Custom']);
const itemsIn = (from, to) => log.slice(from, to).filter(({ event }) => STARTS.has(event.type)).length;
const runStart = (i) => { while (i > 0 && !log[i].turn) i--; return i; };

// State documents by key, each sent whole. A snapshot restates every one.
const documents = new Map();
const publish = (key, mediaType, snapshot) => { documents.set(key, emit('StateSnapshot', { key, mediaType, snapshot })); };
let status = null;
const setStatus = (value) => {
  if (value === status) return;
  status = value;
  publish('status', 'application/json', value);
};

const hello = (snapshot) => ({ type: 'Hello', contract: 2, threadId,
  capabilities: { input: true, modes: ['steer', 'queue'], commands: true, roots: [{ path: cwd, local: true }] },
  ...(snapshot ? { snapshot } : {}) });

function watch(res, q) {
  res.writeHead(200, { 'Content-Type': 'application/jsonl', 'Cache-Control': 'no-store' });
  const after = q.has('after') ? Number(q.get('after')) - base : NaN;
  const write = (event) => res.write(JSON.stringify(event) + '\n');
  if (after >= Math.max(0, log.length - buffer) && after <= log.length) {
    write(hello());
    for (const { line } of log.slice(after)) res.write(line);
  } else {
    let start = log.length, bytes = 0;
    while (start > 0 && bytes < pageBytes) bytes += log[--start].line.length;
    start = runStart(start);
    write(hello({ earlierItems: itemsIn(0, start), oldestSeq: base + start + 1 }));
    for (const record of documents.values()) if (log.indexOf(record) < start) res.write(record.line);
    for (const { line } of log.slice(start)) res.write(line);
  }
  watchers.add(res);
  res.on('close', () => watchers.delete(res));
}

function page(q) {
  const before = Math.max(0, Math.min(Number(q.get('before') ?? base + log.length + 1) - base - 1, log.length));
  const asked = Math.trunc(Number(q.get('limit') ?? 50));
  const limit = Math.min(asked > 0 ? asked : 50, 500);
  let start = before;
  while (start > 0 && itemsIn(start, before) < limit) start--;
  start = runStart(start);
  return { events: log.slice(start, before).map(({ event }) => event), earlierItems: itemsIn(0, start), oldestSeq: base + start + 1 };
}

// ---- history: the session log through the replay translator, so live and
// replayed threads come from one mapping of Claude's tools.
const translator = fileURLToPath(new URL('../claude-to-events.py', import.meta.url));
function history() {
  // Claude hashes long project directory names, so the log is found by its session id.
  const projects = join(process.env.CLAUDE_CONFIG_DIR ?? join(homedir(), '.claude'), 'projects');
  const file = readdirSync(projects).map((dir) => join(projects, dir, `${threadId}.jsonl`)).find(existsSync);
  if (!file) throw new Error(`no session log for ${threadId} in ${projects}`);
  const out = execFileSync('python3', [translator, file], { maxBuffer: 1 << 30, encoding: 'utf-8' });
  let turns = 0;
  for (const line of out.split('\n')) {
    if (!line) continue;
    // Subagents' threads are #700's; this serves the main thread only.
    const { type, threadId: thread, seq, source, ...fields } = JSON.parse(line);
    if (type === 'Hello' || thread !== threadId) continue;
    const record = emit(type, fields);
    if (type === 'RunStarted') { record.turn = true; turns++; }
  }
  return turns;
}

// ---- tools, as the translator titles and kinds them.
const KINDS = { Bash: 'shell', Edit: 'edit', MultiEdit: 'edit', Write: 'edit', NotebookEdit: 'edit', Read: 'read',
  Grep: 'search', Glob: 'search', Agent: 'agent', Task: 'agent', WebFetch: 'web', WebSearch: 'web' };
const PATH_TITLES = { Read: 'file_path', Write: 'file_path', Edit: 'file_path', MultiEdit: 'file_path', NotebookEdit: 'notebook_path' };
const SUMMARY_TITLES = { Grep: 'pattern', Glob: 'pattern', Agent: 'description', Task: 'description', WebFetch: 'url',
  WebSearch: 'query', Skill: 'skill' };
const kindOf = (name) => (name.startsWith('mcp__') ? 'mcp' : KINDS[name] ?? 'other');
const shorten = (text) => { const t = text.split(/\s+/).filter(Boolean).join(' '); return t.length <= 120 ? t : `${t.slice(0, 119)}…`; };
function titleOf(name, input) {
  for (const key of name === 'Bash' ? ['description', 'command'] : []) if (typeof input[key] === 'string' && input[key].trim()) return shorten(input[key]);
  const key = PATH_TITLES[name] ?? SUMMARY_TITLES[name];
  if (typeof input[key] === 'string') return shorten(input[key]);
  if (name.startsWith('mcp__')) { const [server, tool] = name.slice(5).split('__'); return tool ? `${server}: ${tool}` : server; }
  const first = Object.values(input).find((v) => typeof v === 'string' && v.trim());
  return first ? shorten(first) : name;
}
const resultText = (content) => (typeof content === 'string' ? content
  : Array.isArray(content) ? content.filter((b) => b.type === 'text').map((b) => b.text).join('\n') : '');
function unifiedDiff(path, hunks) {
  const lines = [`--- ${path}`, `+++ ${path}`];
  for (const h of hunks) lines.push(`@@ -${h.oldStart ?? 0},${h.oldLines ?? 0} +${h.newStart ?? 0},${h.newLines ?? 0} @@`, ...(h.lines ?? []));
  return lines.join('\n') + '\n';
}

// ---- the checklist: TodoWrite sends the whole list; TaskCreate and
// TaskUpdate change one entry each, so the list is kept here.
let checklist = [];
const publishChecklist = () => publish('checklist', 'application/vnd.flotilla.checklist+json', checklist);
function checklistCall(name, input, result) {
  if (name === 'TodoWrite' && Array.isArray(input.todos)) {
    checklist = input.todos.map((t, i) => ({ id: String(i + 1), content: t.content, status: t.status, ...(t.activeForm ? { activeForm: t.activeForm } : {}) }));
  } else if (name === 'TaskCreate' && result?.task?.id) {
    checklist.push({ id: result.task.id, content: result.task.subject, status: 'pending', ...(input.activeForm ? { activeForm: input.activeForm } : {}) });
  } else if (name === 'TaskUpdate' && result?.success) {
    const entry = checklist.find((t) => t.id === input.taskId);
    if (!entry) return;
    if (input.status === 'deleted') checklist = checklist.filter((t) => t !== entry);
    else Object.assign(entry, input.status ? { status: input.status } : {}, input.subject ? { content: input.subject } : {},
      input.activeForm ? { activeForm: input.activeForm } : {});
  } else return;
  publishChecklist();
}

// ---- tasks: subagents and background shells, from the SDK's task messages.
const tasks = new Map();
const publishTasks = () => publish('tasks', 'application/vnd.flotilla.tasks+json', [...tasks.values()]);
const TASK_KINDS = { local_agent: 'subagent', remote_agent: 'subagent', local_bash: 'shell', monitor: 'monitor' };
const ENDED = { completed: 'completed', failed: 'failed', killed: 'cancelled', stopped: 'cancelled' };
function taskMessage(m) {
  if (m.subtype === 'task_started') {
    if (!TASK_KINDS[m.task_type]) say(`\n[task type ${m.task_type}]\n`);
    tasks.set(m.task_id, { id: m.task_id, kind: TASK_KINDS[m.task_type] ?? 'other', title: m.description, status: 'running',
      startedAt: Date.now(), ...(m.tool_use_id ? { toolCallId: m.tool_use_id } : {}), awaited: !m.is_backgrounded });
  } else {
    const task = tasks.get(m.task_id);
    const ended = ENDED[m.subtype === 'task_updated' ? m.patch?.status : m.status];
    if (!task || !ended || task.endedAt) return;
    Object.assign(task, { status: ended, endedAt: Date.now() }, m.output_file ? { output: { path: m.output_file } } : {});
  }
  publishTasks();
}

// ---- commands: the harness's list, less what only a terminal can run (#694 C3).
let terminalOnly = new Set();
const publishCommands = (commands) => publish('commands', 'application/vnd.flotilla.commands+json', commands
  .filter((c) => !terminalOnly.has(c.name))
  .map((c) => ({ name: c.name, description: c.description, ...(c.argumentHint ? { hint: c.argumentHint } : {}),
    ...(c.aliases?.length ? { aliases: c.aliases } : {}), insert: `/${c.name} ` })));

// ---- the live stream. A turn is cut into AG-UI runs where it waits on an
// Interrupt, as for Codex.
let turnOpen = false;
let runId = null;
let runOpen = false;
let stopping = false;
const startRun = (input = {}) => {
  runId = randomUUID();
  runOpen = true;
  const record = emit('RunStarted', { runId, ...(Object.keys(input).length ? { input } : {}) });
  // Claude starts turns of its own too, as when a background task ends.
  if (!turnOpen) { turnOpen = true; record.turn = true; setStatus('running'); }
};
const finishRun = (outcome) => { if (runOpen) emit('RunFinished', { runId, ...(outcome ? { outcome } : {}) }); runOpen = false; };

// One API message is spread over stream events; blocks are keyed by index.
let messageId = null;
let blocks = new Map();
const blockIds = new Map();
function blockId(suffix) {
  const key = `${messageId}\0${suffix}`;
  const n = blockIds.get(key) ?? 0;
  blockIds.set(key, n + 1);
  const id = suffix ? `${messageId}.${suffix}` : messageId;
  return n ? `${id}.${n}` : id;
}
const calls = new Map();
const streamedIds = new Set();

// A local command's output (/context, /cost) comes whole, as an assistant
// message the model never wrote, so no stream event carries it.
function unstreamed(message) {
  if (streamedIds.has(message.id)) return;
  messageId = message.id;
  for (const b of message.content ?? []) {
    if (b.type !== 'text' || !b.text) continue;
    if (!runOpen) startRun();
    const id = blockId('');
    emit('TextMessageStart', { messageId: id, role: 'assistant' });
    emit('TextMessageContent', { messageId: id, delta: b.text });
    emit('TextMessageEnd', { messageId: id });
  }
}

function streamEvent(e) {
  switch (e.type) {
    case 'message_start': messageId = e.message.id; blocks = new Map(); streamedIds.add(messageId); break;
    case 'content_block_start': {
      const b = e.content_block;
      if (!runOpen) startRun();
      if (b.type === 'text') blocks.set(e.index, { type: 'text', id: blockId(''), open: false });
      else if (b.type === 'thinking') blocks.set(e.index, { type: 'thinking', id: blockId('reasoning'), open: false });
      else if (b.type === 'tool_use') {
        blocks.set(e.index, { type: 'tool_use', id: b.id, name: b.name, json: '' });
        emit('ToolCallStart', { toolCallId: b.id, toolCallName: b.name, kind: kindOf(b.name), parentMessageId: messageId, timestamp: Date.now() });
      }
      break;
    }
    case 'content_block_delta': {
      const b = blocks.get(e.index);
      const d = e.delta;
      if (!b) break;
      if (d.type === 'text_delta' && d.text) {
        if (!b.open) { b.open = true; emit('TextMessageStart', { messageId: b.id, role: 'assistant' }); }
        emit('TextMessageContent', { messageId: b.id, delta: d.text }); say(d.text);
      } else if (d.type === 'thinking_delta' && d.thinking) {
        // Thinking shows only when the model returns its summary; omitted
        // thinking streams no text, and an empty Block would show as a gap.
        if (!b.open) { b.open = true; emit('ReasoningMessageStart', { messageId: b.id }); }
        emit('ReasoningMessageContent', { messageId: b.id, delta: d.thinking });
      } else if (d.type === 'input_json_delta' && d.partial_json) {
        b.json += d.partial_json;
        emit('ToolCallArgs', { toolCallId: b.id, delta: d.partial_json });
      }
      break;
    }
    case 'content_block_stop': {
      const b = blocks.get(e.index);
      if (b?.type === 'text' && b.open) emit('TextMessageEnd', { messageId: b.id });
      else if (b?.type === 'thinking' && b.open) emit('ReasoningMessageEnd', { messageId: b.id });
      else if (b?.type === 'tool_use') {
        let input = {};
        try { input = JSON.parse(b.json || '{}'); } catch {}
        calls.set(b.id, { name: b.name, input });
        // The title waits for the whole input, so it arrives on the call's end.
        emit('ToolCallEnd', { toolCallId: b.id, title: titleOf(b.name, input) });
        if (b.name === 'TodoWrite') checklistCall(b.name, input);
        if (b.name === 'Bash') say(`\n$ ${input.command}\n`);
      }
      break;
    }
  }
}

function toolResult(block, structured) {
  const call = calls.get(block.tool_use_id);
  if (!call) return;
  calls.delete(block.tool_use_id);
  const side = structured && typeof structured === 'object' ? structured : {};
  let content = resultText(block.content);
  // A call refused because the turn was stopped ends as cancelled, not failed.
  const status = side.interrupted === true || (block.is_error && stopping) ? 'cancelled' : block.is_error ? 'failed' : 'ok';
  let fields = { mediaType: call.name === 'Bash' ? 'text/x-ansi' : 'text/plain' };
  if (Array.isArray(side.structuredPatch)) {
    content = unifiedDiff(side.filePath ?? '', side.structuredPatch);
    fields = { mediaType: 'text/x-diff', structured: { filePath: side.filePath ?? '', hunks: side.structuredPatch } };
  }
  emit('ToolCallResult', { toolCallId: block.tool_use_id, messageId: `${block.tool_use_id}-result`, status, ...fields, content });
  checklistCall(call.name, call.input, side);
}

// Every SDK message as received, to see what the harness sends.
const trace = opt('--trace');

function onMessage(m) {
  if (trace) appendFileSync(trace, JSON.stringify(m) + '\n');
  // Subagents' messages carry the call that started them; their threads are #700's.
  if (m.parent_tool_use_id) return;
  switch (m.type) {
    case 'stream_event': streamEvent(m.event); break;
    case 'assistant': unstreamed(m.message); break;
    case 'user':
      if (Array.isArray(m.message?.content)) for (const b of m.message.content) if (b.type === 'tool_result') toolResult(b, m.tool_use_result);
      break;
    case 'result': {
      // What still waits when the turn ends will never be answered.
      if (waits.size) answer([...waits.keys()].map((interruptId) => ({ interruptId, status: 'cancelled' })), false);
      if (!runOpen) startRun();
      finishRun(stopping ? { type: 'cancelled' } : undefined);
      if (m.subtype !== 'success') say(`\n[turn ended: ${m.subtype}]\n`);
      turnOpen = false; stopping = false;
      say('\n[turn done]\n> ');
      startQueued();
      break;
    }
    case 'system':
      if (m.subtype === 'init') terminalOnly = new Set(m.terminal_slash_commands ?? []);
      else if (m.subtype === 'commands_changed') publishCommands(m.commands);
      else if (m.subtype === 'compact_boundary') emit('Custom', { name: 'compaction', value: {} });
      else if (m.subtype?.startsWith('task_')) taskMessage(m);
      break;
  }
}

// ---- approvals and questions: canUseTool holds the turn until answered.
const waits = new Map();
const answers = new Map();

function interrupt() {
  if (!runOpen) startRun();
  emit('RunFinished', { runId, outcome: { type: 'interrupt', interrupts: [...waits.values()].map((w) => w.interrupt) } });
  runOpen = false;
  setStatus('requires_action');
}

// AskUserQuestion's questions, keyed by position: their text is the answer's key.
function questionInterrupt(id, input) {
  const properties = {};
  input.questions.forEach((q, i) => {
    const choices = [...(q.options ?? []).map((o) => ({ const: o.label, title: o.label, description: o.description })),
      { type: 'string', title: 'Other' }];
    properties[`q${i}`] = { title: q.header, description: q.question,
      ...(q.multiSelect ? { type: 'array', items: { anyOf: choices } } : { anyOf: choices }) };
  });
  return { id, reason: 'question', responseSchema: { type: 'object', required: Object.keys(properties), properties } };
}

function approvalInterrupt(id, toolName, options) {
  const decisions = [['allow', 'Approve'], ...(options.suggestions?.length ? [['allowSession', 'Approve for this session']] : []), ['deny', 'Deny']];
  const message = options.title ?? options.description ?? options.decisionReason;
  return { id, reason: 'approval', ...(typeof message === 'string' ? { message } : {}),
    ...(calls.has(options.toolUseID) ? { toolCallId: options.toolUseID } : {}),
    responseSchema: { type: 'object', required: ['decision'], properties: {
      decision: { title: 'Decision', oneOf: decisions.map(([value, title]) => ({ const: value, title })) } } } };
}

function canUseTool(toolName, input, options) {
  if (stopping) return Promise.resolve({ behavior: 'deny', message: 'The user stopped the turn.', interrupt: true });
  const id = options.toolUseID ?? randomUUID();
  return new Promise((resolve) => {
    const question = toolName === 'AskUserQuestion' && Array.isArray(input.questions);
    waits.set(id, { resolve, toolName, input, suggestions: options.suggestions,
      interrupt: question ? questionInterrupt(id, input) : approvalInterrupt(id, toolName, options) });
    say(`\n[${question ? 'question' : 'approval'} ${id}: ${toolName}]\n> `);
    interrupt();
  });
}

// Claude's answer to what it waits on, from a resume entry; null if the entry
// names no choice it offered.
function permission(waiting, entry) {
  const payload = entry.payload ?? {};
  if (waiting.interrupt.reason === 'question') {
    if (entry.status === 'cancelled') return { behavior: 'deny', message: 'The user dismissed the question.' };
    // Free text is a choice, so an answer need only have the schema's shape.
    const shaped = (q, v) => typeof v === 'string' || (q.multiSelect && Array.isArray(v) && v.every((s) => typeof s === 'string'));
    if (!waiting.input.questions.every((q, i) => shaped(q, payload[`q${i}`]))) return null;
    const given = {};
    waiting.input.questions.forEach((q, i) => {
      const v = payload[`q${i}`];
      given[q.question] = Array.isArray(v) ? v.join(', ') : v;
    });
    return { behavior: 'allow', updatedInput: { ...waiting.input, answers: given } };
  }
  const decision = entry.status === 'cancelled' ? 'deny' : payload.decision;
  if (decision === 'allow') return { behavior: 'allow', updatedInput: waiting.input };
  if (decision === 'allowSession' && waiting.suggestions?.length) {
    return { behavior: 'allow', updatedInput: waiting.input, updatedPermissions: waiting.suggestions };
  }
  if (decision === 'deny') return { behavior: 'deny', message: entry.status === 'cancelled' ? 'The user stopped the turn.' : 'The user denied this.' };
  return null;
}

function answer(entries, reopen = true) {
  for (const entry of entries) {
    const waiting = waits.get(entry.interruptId);
    waits.delete(entry.interruptId);
    answers.set(entry.interruptId, entry);
    waiting.resolve(permission(waiting, entry));
  }
  startRun({ resume: entries });
  if (waits.size && reopen) interrupt();
  else setStatus(turnOpen ? 'running' : 'idle');
}

// ---- input: one long-lived query fed by this queue of user messages.
const inbox = [];
let wake = null;
async function* prompts() {
  for (;;) {
    while (inbox.length) yield inbox.shift();
    await new Promise((resolve) => { wake = resolve; });
  }
}

const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
const queued = [];
function startQueued() {
  const next = queued.shift();
  if (!next) return setStatus('idle');
  send(next.text, next.clientId);
}

// A prompt sent while a turn runs joins it (steer); `queue` holds it for the
// turn's end, which the SDK leaves to its host.
function send(text, clientId = randomUUID(), mode) {
  if (turnOpen && mode === 'queue') return queued.push({ text, clientId });
  if (!turnOpen) startRun();
  setStatus('running');
  emit('TextMessageStart', { messageId: clientId, role: 'user' });
  emit('TextMessageContent', { messageId: clientId, delta: text });
  emit('TextMessageEnd', { messageId: clientId });
  // A UUID id is kept as the log record's, so a replay names the prompt as its sender did.
  inbox.push({ type: 'user', message: { role: 'user', content: text }, parent_tool_use_id: null, session_id: threadId,
    origin: { kind: 'human' }, ...(UUID.test(clientId) ? { uuid: clientId } : {}) });
  wake?.();
}

async function stop() {
  queued.length = 0;
  if (!turnOpen) return false;
  stopping = true;
  if (waits.size) answer([...waits.keys()].map((interruptId) => ({ interruptId, status: 'cancelled' })), false);
  await session.interrupt();
  return true;
}

function run(input) {
  const resume = input.resume ?? [];
  const message = (input.messages ?? []).filter((m) => m.role === 'user').at(-1);
  const text = typeof message?.content === 'string' ? message.content
    : (message?.content ?? []).filter((c) => c.type === 'text').map((c) => c.text).join('\n');
  if (message ? !text : !resume.length) return [400, { error: 'no user message' }];
  if (new Set(resume.map((e) => e.interruptId)).size !== resume.length) return [400, { error: 'an interrupt answered twice' }];
  for (const entry of resume) {
    if (answers.has(entry.interruptId)) return [409, { error: 'answered', by: answers.get(entry.interruptId) }];
    const waiting = waits.get(entry.interruptId);
    if (!waiting) return [404, { error: `no interrupt ${entry.interruptId}` }];
    if (!permission(waiting, entry)) return [400, { error: `no such choice for ${entry.interruptId}` }];
  }
  if (resume.length) answer(resume);
  if (message) send(text, message.id, input.mode);
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
        const [, threads, id, verb, taskId, taskVerb] = pathname.split('/').map(decodeURIComponent);
        if (threads !== 'threads') return reply(404, { error: pathname });
        if (id == null || id === '') {
          const listed = await listSessions({ dir: cwd, limit: 50 }).catch(() => []);
          const all = listed.map((s) => ({ threadId: s.sessionId, title: s.customTitle ?? s.summary, cwd: s.cwd }));
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
        if (verb === 'stop') return reply(200, { stopped: await stop() });
        if (verb === 'tasks' && taskVerb === 'stop') {
          if (!tasks.has(taskId)) return reply(404, { error: `no task ${taskId}` });
          await session.stopTask(taskId);
          return reply(200, { stopped: true });
        }
        if (verb !== 'run') return reply(404, { error: pathname });
        let input;
        try { input = JSON.parse(body || '{}'); } catch { return reply(400, { error: 'the body is not JSON' }); }
        return reply(...run(input));
      } catch (err) {
        return reply(500, { error: err.message });
      }
    });
  }).listen(socket, () => process.umask(umask));
}

const say = (s) => process.stdout.write(s);

if (resumed) cwd = opt('--cwd') ?? (await getSessionInfo(threadId))?.cwd ?? cwd;
const turns = resumed ? history() : 0;
setStatus('idle');

const session = query({ prompt: prompts(), options: {
  cwd, permissionMode, settingSources, canUseTool, includePartialMessages: true,
  ...(resumed ? { resume: threadId } : { sessionId: threadId }),
  ...(opt('--model') ? { model: opt('--model') } : {}),
  stderr: (data) => process.stderr.write(data),
} });
session.supportedCommands().then(publishCommands, (err) => say(`\n[commands] ${err.message}\n`));

serve();
say(`session ${threadId}: ${turns} turns of history, ${log.length} events, on ${socket}\n> `);
createInterface({ input: process.stdin }).on('line', async (line) => {
  const text = line.trim();
  if (!text) return;
  try {
    const approval = [...waits.entries()].find(([, w]) => w.interrupt.reason === 'approval');
    if (approval && /^[yn]/i.test(text)) {
      answer([{ interruptId: approval[0], status: 'resolved', payload: { decision: /^y/i.test(text) ? 'allow' : 'deny' } }]);
    } else if (text === '/interrupt') {
      await stop();
    } else {
      send(text);
    }
  } catch (err) { say(`\n[error] ${err.message}\n> `); }
});

try {
  for await (const m of session) onMessage(m);
} catch (err) {
  say(`\n[session ended] ${err.message}\n`);
  process.exit(1);
}
say('\n[session closed]\n');
process.exit(0);
