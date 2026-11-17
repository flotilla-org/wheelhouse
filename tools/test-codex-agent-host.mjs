#!/usr/bin/env node
// tools/codex-agent-host.mjs against a scripted fake app-server: its state
// documents, child threads and routes.
//
//   node tools/test-codex-agent-host.mjs
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdtempSync, rmSync } from 'node:fs';
import { createServer, request as httpRequest } from 'node:http';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { after, before, test } from 'node:test';
import { fileURLToPath } from 'node:url';

const BRIDGE = fileURLToPath(new URL('./codex-agent-host.mjs', import.meta.url));
const MAIN = 'th-main';
const CHILD = 'th-child';

// A WebSocket server just big enough for one client's unfragmented text frames.
function fakeAppServer(handlers) {
  const requests = [];
  let client = null;
  const send = (message) => {
    const data = Buffer.from(JSON.stringify(message));
    const head = data.length < 126 ? Buffer.from([0x81, data.length])
      : data.length < 65536 ? Buffer.from([0x81, 126, data.length >> 8, data.length & 255])
      : Buffer.concat([Buffer.from([0x81, 127]), (() => { const b = Buffer.alloc(8); b.writeBigUInt64BE(BigInt(data.length)); return b; })()]);
    client.write(Buffer.concat([head, data]));
  };
  const onMessage = (msg) => {
    if (msg.method == null) return;
    requests.push(msg);
    if (msg.id == null) return;
    const handler = handlers[msg.method];
    send({ id: msg.id, result: handler ? handler(msg.params) : {} });
  };
  const server = createServer();
  server.on('upgrade', (req, sock) => {
    const accept = createHash('sha1').update(`${req.headers['sec-websocket-key']}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`).digest('base64');
    sock.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
    client = sock;
    let pending = Buffer.alloc(0);
    sock.on('data', (chunk) => {
      pending = Buffer.concat([pending, chunk]);
      for (;;) {
        if (pending.length < 2) return;
        const opcode = pending[0] & 15;
        let length = pending[1] & 127, at = 2;
        if (length === 126) { length = pending.readUInt16BE(2); at = 4; }
        else if (length === 127) { length = Number(pending.readBigUInt64BE(2)); at = 10; }
        if (pending.length < at + 4 + length) return;
        const mask = pending.subarray(at, at + 4);
        const payload = Buffer.from(pending.subarray(at + 4, at + 4 + length).map((b, i) => b ^ mask[i % 4]));
        pending = pending.subarray(at + 4 + length);
        if (opcode === 1) onMessage(JSON.parse(payload.toString()));
        else if (opcode === 8) sock.end();
      }
    });
    sock.on('error', () => {});
  });
  return {
    requests,
    notify: (method, params) => send({ method, params }),
    listen: () => new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve(server.address().port))),
    close: () => { client?.destroy(); server.close(); },
  };
}

const skills = [[{ name: 'lint', description: 'Lint the code', shortDescription: 'Lint', enabled: true, path: '/s/lint', scope: 'repo' },
  { name: 'off', description: 'Disabled', enabled: false, path: '/s/off', scope: 'repo' }]];
const app = fakeAppServer({
  'thread/start': () => ({ thread: { id: MAIN } }),
  'thread/list': () => ({ data: [{ id: MAIN, name: 'Main', cwd: '/w' }, { id: 'th-other', preview: 'Other', cwd: '/w' }], nextCursor: null }),
  'skills/list': () => ({ data: [{ cwd: '/w', skills: skills[0], errors: [] }] }),
  'thread/backgroundTerminals/terminate': () => ({ terminated: true }),
  fuzzyFileSearch: () => ({ files: [
    { root: '/w', path: 'src/lib.rs', match_type: 'file', file_name: 'lib.rs', score: 9, indices: null },
    { root: '/w', path: 'src', match_type: 'directory', file_name: 'src', score: 5, indices: null }] }),
});

const until = async (predicate, what) => {
  const deadline = Date.now() + 5000;
  for (;;) {
    const found = predicate();
    if (found) return found;
    if (Date.now() > deadline) throw new Error(`timed out waiting for ${what}`);
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
};
const requested = async (method, match = () => true) => (await until(() => app.requests.findLast((r) => r.method === method && match(r.params)), method)).params;

let dir, socket, bridge;
const call = (method, path, body) => new Promise((resolve, reject) => {
  const req = httpRequest({ socketPath: socket, method, path }, (res) => {
    let text = '';
    res.on('data', (chunk) => { text += chunk; });
    res.on('end', () => resolve({ status: res.statusCode, text, json: () => JSON.parse(text) }));
  });
  req.on('error', reject);
  req.end(body ? JSON.stringify(body) : undefined);
});
const watches = [];
function watch(thread) {
  const events = [];
  const req = httpRequest({ socketPath: socket, path: `/threads/${thread}/events` }, (res) => {
    let rest = '';
    res.on('data', (chunk) => {
      const lines = (rest + chunk).split('\n');
      rest = lines.pop();
      events.push(...lines.map((line) => JSON.parse(line)));
    });
  });
  req.on('error', () => {});
  req.end();
  watches.push(req);
  return events;
}
const documents = (events, key) => events.filter((e) => e.type === 'StateSnapshot' && e.key === key).map((e) => e.snapshot);
const latest = (events, key, predicate = () => true, what = key) => until(() => documents(events, key).findLast(predicate), what);
const prompt = (thread, text) => call('POST', `/threads/${thread}/run`, { messages: [{ id: `m-${text}`, role: 'user', content: text }] });

before(async () => {
  const port = await app.listen();
  dir = mkdtempSync(join(tmpdir(), 'codex-agent-host-'));
  socket = join(dir, 'agent.sock');
  bridge = spawn(process.execPath, [BRIDGE, '--url', `ws://127.0.0.1:${port}`, '--new', '--cwd', '/w', '--socket', socket],
    { stdio: ['pipe', 'pipe', 'inherit'] });
  let out = '';
  bridge.stdout.on('data', (chunk) => { out += chunk; });
  await until(() => out.includes(`on ${socket}`), 'the bridge to serve');
});

after(() => {
  for (const req of watches) req.destroy();
  bridge?.kill();
  app.close();
  rmSync(dir, { recursive: true, force: true });
});

test('the main thread offers commands and file search', async () => {
  assert.deepEqual((await requested('initialize')).capabilities, { experimentalApi: true });
  const events = watch(MAIN);
  const hello = await until(() => events[0], 'hello');
  assert.equal(hello.capabilities.commands, true);
  assert.deepEqual(hello.capabilities.roots, [{ path: '/w', local: true, search: `/threads/${MAIN}/files` }]);
  const commands = await latest(events, 'commands');
  assert.deepEqual(commands.find((c) => c.name === 'compact'), { name: 'compact', description: 'Summarize the conversation to free context', insert: '/compact ' });
  assert.equal(commands.find((c) => c.name === 'review').hint, '[instructions]');
  assert.deepEqual(commands.filter((c) => c.group === 'Skills'), [{ name: 'lint', description: 'Lint', insert: '$lint ', group: 'Skills' }]);
});

test('a skills change republishes the commands', async () => {
  const events = watch(MAIN);
  skills[0] = [...skills[0], { name: 'deploy', description: 'Deploy it', enabled: true, path: '/s/deploy', scope: 'user' }];
  app.notify('skills/changed', {});
  const commands = await latest(events, 'commands', (c) => c.some((e) => e.name === 'deploy'), 'the new skill');
  assert.deepEqual(commands.at(-1), { name: 'deploy', description: 'Deploy it', insert: '$deploy ', group: 'Skills' });
  assert.equal((await requested('skills/list', (p) => p.forceReload)).forceReload, true);
});

test('command texts become app-server calls and other text goes as typed', async () => {
  const events = watch(MAIN);
  assert.equal((await prompt(MAIN, '/compact')).status, 202);
  assert.deepEqual((await requested('thread/compact/start')), { threadId: MAIN });
  assert.ok(events.some((e) => e.type === 'Custom' && e.name === 'command' && e.value.name === 'compact'));
  await prompt(MAIN, '/review the parser');
  assert.deepEqual(await requested('review/start'), { threadId: MAIN, target: { type: 'custom', instructions: 'the parser' } });
  await prompt(MAIN, '/review-branch main');
  assert.deepEqual((await requested('review/start', (p) => p.target.type === 'baseBranch')).target, { type: 'baseBranch', branch: 'main' });
  await prompt(MAIN, '/init');
  assert.match((await requested('turn/start', (p) => p.clientUserMessageId === 'm-/init')).input[0].text, /AGENTS\.md/);
  await prompt(MAIN, '/plan fast');
  assert.deepEqual((await requested('turn/start', (p) => p.clientUserMessageId === 'm-/plan fast')).input, [{ type: 'text', text: '/plan fast' }]);
});

test('a plan update is the checklist', async () => {
  const events = watch(MAIN);
  app.notify('turn/plan/updated', { threadId: MAIN, turnId: 't0', explanation: null,
    plan: [{ step: 'Read the code', status: 'completed' }, { step: 'Fix it', status: 'inProgress' }, { step: 'Test it', status: 'pending' }] });
  assert.deepEqual(await latest(events, 'checklist'), [
    { id: '1', content: 'Read the code', status: 'completed' },
    { id: '2', content: 'Fix it', status: 'in_progress' },
    { id: '3', content: 'Test it', status: 'pending' }]);
});

test('a subagent is a task and a child thread, and stops', async () => {
  const events = watch(MAIN);
  app.notify('turn/started', { threadId: MAIN, turn: { id: 't1', items: [], status: 'inProgress' } });
  app.notify('item/started', { threadId: MAIN, turnId: 't1', startedAtMs: 1000, item: { type: 'collabAgentToolCall', id: 'call-spawn',
    tool: 'spawnAgent', status: 'inProgress', senderThreadId: MAIN, receiverThreadIds: [], prompt: 'Audit the lexer', agentsStates: {} } });
  // The child's first events can come before the call that names it.
  app.notify('turn/started', { threadId: CHILD, turn: { id: 'ct1', items: [], status: 'inProgress' } });
  app.notify('item/completed', { threadId: CHILD, turnId: 'ct1', item: { type: 'agentMessage', id: 'cm1', text: 'Reading the lexer.' } });
  app.notify('item/completed', { threadId: MAIN, turnId: 't1', completedAtMs: 2000, item: { type: 'collabAgentToolCall', id: 'call-spawn',
    tool: 'spawnAgent', status: 'completed', senderThreadId: MAIN, receiverThreadIds: [CHILD], prompt: 'Audit the lexer',
    agentsStates: { [CHILD]: { status: 'running', message: null } } } });
  const [task] = await latest(events, 'tasks');
  assert.deepEqual(task, { id: CHILD, kind: 'subagent', title: 'Audit the lexer', status: 'running', startedAt: 2000,
    toolCallId: 'call-spawn', childThreadId: CHILD, awaited: false });
  assert.ok(events.some((e) => e.type === 'ToolCallStart' && e.toolCallId === 'call-spawn' && e.kind === 'agent'));

  const listed = (await call('GET', '/threads')).json();
  assert.deepEqual(listed.map((t) => t.threadId), [MAIN, CHILD, 'th-other']);
  assert.deepEqual(listed[1].parent, { threadId: MAIN, toolCallId: 'call-spawn' });
  assert.equal(listed[0].title, 'Main');

  const child = watch(CHILD);
  assert.deepEqual((await until(() => child.find((e) => e.type === 'RunStarted'), 'the child run')).parent, { threadId: MAIN, toolCallId: 'call-spawn' });
  assert.equal((await until(() => child[0], 'the child hello')).capabilities.input, false);
  await until(() => child.some((e) => e.type === 'TextMessageContent' && e.delta === 'Reading the lexer.'), 'the child message');
  // A subagent's checklist is its own thread's.
  app.notify('turn/plan/updated', { threadId: CHILD, turnId: 'ct1', plan: [{ step: 'Scan tokens', status: 'inProgress' }] });
  assert.deepEqual(await latest(child, 'checklist'), [{ id: '1', content: 'Scan tokens', status: 'in_progress' }]);
  assert.equal((await prompt(CHILD, 'hello')).status, 409);

  assert.deepEqual((await call('POST', `/threads/${MAIN}/tasks/${CHILD}/stop`)).json(), { stopped: true });
  assert.deepEqual(await requested('turn/interrupt'), { threadId: CHILD, turnId: 'ct1' });
  app.notify('turn/completed', { threadId: CHILD, turn: { id: 'ct1', items: [], status: 'interrupted' } });
  const [stopped] = await latest(events, 'tasks', ([t]) => t.status !== 'running', 'the stopped task');
  assert.equal(stopped.status, 'cancelled');
  assert.equal(typeof stopped.endedAt, 'number');
  assert.equal((await call('POST', `/threads/${MAIN}/tasks/nothing/stop`)).status, 404);
  assert.equal((await call('GET', `/threads/${MAIN}/tasks/${CHILD}/output`)).status, 404);
  assert.equal((await call('GET', `/threads/${MAIN}/foo/x/output`)).status, 405);
});

test('a terminal left running is a task until it ends', async () => {
  const events = watch(MAIN);
  app.notify('item/started', { threadId: MAIN, turnId: 't1', startedAtMs: 3000, item: { type: 'commandExecution', id: 'cmd-dev',
    command: 'npm run dev', cwd: '/w', processId: '4242', source: 'unifiedExecStartup', status: 'inProgress', commandActions: [] } });
  app.notify('item/started', { threadId: MAIN, turnId: 't1', item: { type: 'commandExecution', id: 'cmd-ls',
    command: 'ls', cwd: '/w', processId: '4243', status: 'inProgress', commandActions: [] } });
  app.notify('item/commandExecution/outputDelta', { threadId: MAIN, turnId: 't1', itemId: 'cmd-dev', delta: 'listening on 3000\n' });
  app.notify('item/completed', { threadId: MAIN, turnId: 't1', item: { type: 'commandExecution', id: 'cmd-ls',
    command: 'ls', cwd: '/w', processId: '4243', status: 'completed', exitCode: 0, aggregatedOutput: 'src\n', commandActions: [] } });
  app.notify('turn/completed', { threadId: MAIN, turn: { id: 't1', items: [], status: 'completed' } });
  const tasks = await latest(events, 'tasks', (ts) => ts.some((t) => t.id === 'cmd-dev'), 'the terminal task');
  assert.deepEqual(tasks.map((t) => t.id), [CHILD, 'cmd-dev']);
  assert.deepEqual(tasks[1], { id: 'cmd-dev', kind: 'shell', title: 'npm run dev', status: 'running', startedAt: 3000, toolCallId: 'cmd-dev',
    awaited: false, output: { url: `/threads/${MAIN}/tasks/cmd-dev/output`, mediaType: 'text/x-ansi' } });

  const output = await call('GET', tasks[1].output.url);
  assert.deepEqual([output.status, output.text], [200, 'listening on 3000\n']);
  assert.deepEqual((await call('POST', `/threads/${MAIN}/tasks/cmd-dev/stop`)).json(), { stopped: true });
  assert.deepEqual(await requested('thread/backgroundTerminals/terminate'), { threadId: MAIN, processId: '4242' });
  app.notify('item/completed', { threadId: MAIN, turnId: 't1', completedAtMs: 4000, item: { type: 'commandExecution', id: 'cmd-dev',
    command: 'npm run dev', cwd: '/w', processId: '4242', status: 'failed', exitCode: 143, aggregatedOutput: 'listening on 3000\nterminated\n', commandActions: [] } });
  const ended = await latest(events, 'tasks', (ts) => ts[1] && ts[1].status !== 'running', 'the ended terminal');
  assert.deepEqual([ended[1].status, ended[1].endedAt], ['cancelled', 4000]);
  assert.equal((await call('GET', tasks[1].output.url)).text, 'listening on 3000\nterminated\n');
});

test('the root search goes to fuzzyFileSearch', async () => {
  const found = await call('GET', `/threads/${MAIN}/files?q=lib`);
  assert.deepEqual(found.json(), [{ path: 'src/lib.rs', kind: 'file' }, { path: 'src', kind: 'dir' }]);
  assert.deepEqual(await requested('fuzzyFileSearch'), { query: 'lib', roots: ['/w'], cancellationToken: null });
  assert.deepEqual((await call('GET', `/threads/${MAIN}/files?q=lib&limit=1`)).json(), [{ path: 'src/lib.rs', kind: 'file' }]);
});

test('a resumed thread reads its subagents\' history as their own threads', async () => {
  const turns = {
    'th-old': [{ id: 'ot1', status: 'completed', startedAt: 5, items: [
      { type: 'userMessage', id: 'u1', content: [{ type: 'text', text: 'Audit it' }] },
      { type: 'collabAgentToolCall', id: 'old-spawn', tool: 'spawnAgent', status: 'completed', senderThreadId: 'th-old',
        receiverThreadIds: ['th-old-child'], prompt: 'Audit the lexer', agentsStates: { 'th-old-child': { status: 'completed', message: 'Sound.' } } }] }],
    'th-old-child': [{ id: 'oct1', status: 'completed', startedAt: 6, items: [{ type: 'agentMessage', id: 'ocm1', text: 'Sound.' }] }],
  };
  const old = fakeAppServer({
    'thread/turns/list': (p) => ({ data: [...turns[p.threadId]].reverse(), nextCursor: null }),
    'skills/list': () => ({ data: [] }),
  });
  const port = await old.listen();
  const oldSocket = join(dir, 'old.sock');
  const resumed = spawn(process.execPath, [BRIDGE, '--url', `ws://127.0.0.1:${port}`, '--thread', 'th-old', '--cwd', '/w', '--socket', oldSocket],
    { stdio: ['pipe', 'pipe', 'inherit'] });
  try {
    let out = '';
    resumed.stdout.on('data', (chunk) => { out += chunk; });
    await until(() => out.includes(`on ${oldSocket}`), 'the resumed bridge to serve');
    assert.match(out, /thread th-old: 1 turns of history/);
    const read = async (thread) => {
      const res = await new Promise((resolve) => httpRequest({ socketPath: oldSocket, path: `/threads/${thread}/events?before=${Number.MAX_SAFE_INTEGER}` }, resolve).end());
      let text = '';
      for await (const chunk of res) text += chunk;
      return JSON.parse(text).events;
    };
    const parentEvents = await read('th-old');
    assert.deepEqual(documents(parentEvents, 'tasks').at(-1), [{ id: 'th-old-child', kind: 'subagent', title: 'Audit the lexer', status: 'completed',
      startedAt: 5000, toolCallId: 'old-spawn', childThreadId: 'th-old-child', awaited: false, endedAt: 5000 }]);
    const childEvents = await read('th-old-child');
    assert.deepEqual(childEvents.find((e) => e.type === 'RunStarted').parent, { threadId: 'th-old', toolCallId: 'old-spawn' });
    assert.ok(childEvents.some((e) => e.type === 'TextMessageContent' && e.delta === 'Sound.'));
    assert.deepEqual(documents(childEvents, 'status'), ['idle']);
  } finally {
    resumed.kill();
    old.close();
  }
});
