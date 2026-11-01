#!/usr/bin/env python3
"""Translate a Claude Code session transcript into conversation events (#615).

Interim and throwaway: Flotilla's normaliser (flotilla-org/flotilla#4206)
replaces it. The wire format is the one fixed on #615 and decided on #548.
"""
import argparse
import json
from pathlib import Path
import re
import sys
import time

CONTRACT = 1
OUTPUT_CAP = 64 * 1024
TITLE_CAP = 120
POLL_SECONDS = 0.2

KINDS = {
    'Bash': 'shell', 'Edit': 'edit', 'MultiEdit': 'edit', 'Write': 'edit', 'NotebookEdit': 'edit',
    'Read': 'read', 'Grep': 'search', 'Glob': 'search', 'Agent': 'agent', 'Task': 'agent',
    'WebFetch': 'web', 'WebSearch': 'web',
}
PATH_TITLES = {'Read': 'file_path', 'Write': 'file_path', 'Edit': 'file_path',
               'MultiEdit': 'file_path', 'NotebookEdit': 'notebook_path'}
SUMMARY_TITLES = {'Grep': 'pattern', 'Glob': 'pattern', 'Agent': 'description', 'Task': 'description',
                  'WebFetch': 'url', 'WebSearch': 'query', 'Skill': 'skill'}
# Claude Code writes these as the tool_result text when a call is cut short.
INTERRUPT_MARKERS = ('[Request interrupted', '[Tool call interrupted')
PERSISTED = re.compile(r'<persisted-output>\s*Output too large \(([\d.]+)\s*(B|KB|MB)\)\. '
                       r'Full output saved to: (\S+)', re.S)
UNITS = {'B': 1, 'KB': 1024, 'MB': 1024 * 1024}


def kind_of(name):
    if name.startswith('mcp__'):
        return 'mcp'
    return KINDS.get(name, 'other')


def shorten(text):
    text = ' '.join(text.split())
    return text if len(text) <= TITLE_CAP else text[:TITLE_CAP - 1] + '…'


def title_of(name, args):
    if name == 'Bash' and isinstance(args.get('command'), str):
        return shorten(args['command'])
    for table in (PATH_TITLES, SUMMARY_TITLES):
        if isinstance(args.get(table.get(name)), str):
            return shorten(args[table[name]])
    if name.startswith('mcp__'):
        server, _, tool = name[len('mcp__'):].partition('__')
        return f'{server}: {tool}' if tool else server
    first = next((v for v in args.values() if isinstance(v, str) and v.strip()), None)
    return shorten(first) if first else name


def cap(text, ref):
    data = text.encode('utf-8')
    if len(data) <= OUTPUT_CAP:
        return text, None
    return data[:OUTPUT_CAP].decode('utf-8', 'ignore'), {'totalBytes': len(data), 'ref': ref}


def unified_diff(path, hunks):
    lines = [f'--- {path}', f'+++ {path}']
    for hunk in hunks:
        lines.append(f"@@ -{hunk.get('oldStart', 0)},{hunk.get('oldLines', 0)} "
                     f"+{hunk.get('newStart', 0)},{hunk.get('newLines', 0)} @@")
        lines.extend(hunk.get('lines', []))
    return '\n'.join(lines) + '\n'


def result_text(content):
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        return '\n'.join(b.get('text', '') for b in content if isinstance(b, dict) and b.get('type') == 'text')
    return ''


def sole_image(content):
    if isinstance(content, list) and len(content) == 1 and isinstance(content[0], dict) \
            and content[0].get('type') == 'image':
        source = content[0].get('source') or {}
        if source.get('type') == 'base64' and isinstance(source.get('data'), str):
            return source.get('media_type', 'image/png'), source['data']
    return None


class Thread:
    """One threadId: the main session or one subagent."""

    def __init__(self, translator, thread_id, source, parent=None):
        self.translator, self.id, self.source, self.parent = translator, thread_id, source, parent
        self.seq = 0
        self.run = None
        self.run_interrupted = False
        self.calls = {}  # open toolCallId -> name
        self.blocks = {}  # (message.id, kind) -> blocks of that kind seen so far
        self.line = 0

    def emit(self, event_type, **fields):
        self.seq += 1
        self.translator.write({'type': event_type, 'threadId': self.id, 'seq': self.seq, **fields})

    def start_run(self, run_id):
        self.finish_run()
        self.run, self.run_interrupted = run_id, False
        extra = {'parent': self.parent} if self.parent else {}
        self.emit('RunStarted', runId=run_id, **extra)

    def finish_run(self):
        if self.run is not None:
            self.emit('RunFinished', runId=self.run, outcome='interrupted' if self.run_interrupted else 'completed')
            self.run = None

    def ensure_run(self, record):
        if self.run is None:
            self.start_run(record.get('uuid') or f'{self.id}-run{self.seq + 1}')

    def message(self, role, message_id, text):
        self.emit('TextMessageStart', messageId=message_id, role=role)
        self.emit('TextMessageContent', messageId=message_id, delta=text)
        self.emit('TextMessageEnd', messageId=message_id)

    def record(self, record):
        kind = record.get('type')
        if kind == 'user':
            self.user(record)
        elif kind == 'assistant':
            self.assistant(record)
        elif kind == 'system' and record.get('subtype') == 'compact_boundary':
            meta = record.get('compactMetadata') or {}
            value = {k: meta[k] for k in ('trigger', 'preTokens') if k in meta}
            self.ensure_run(record)
            self.emit('Custom', name='compaction', value=value)

    def user(self, record):
        # isMeta lines are injected by the harness and the compact summary
        # stands in for the history before a compaction boundary.
        if record.get('isMeta') or record.get('isCompactSummary'):
            return
        content = (record.get('message') or {}).get('content')
        if isinstance(content, str):
            content = [{'type': 'text', 'text': content}]
        if not isinstance(content, list):
            return
        results = [b for b in content if isinstance(b, dict) and b.get('type') == 'tool_result']
        text = '\n'.join(b.get('text', '') for b in content
                         if isinstance(b, dict) and b.get('type') == 'text').strip()
        for block in results:
            self.result(record, block)
        if not text:
            return
        if text.startswith(INTERRUPT_MARKERS):
            self.run_interrupted = True
            return
        if not results:
            self.finish_run()  # a typed prompt opens a new turn
        self.ensure_run(record)
        self.message('user', record.get('uuid') or f'{self.id}-m{self.seq + 1}', text)

    def assistant(self, record):
        message = record.get('message') or {}
        message_id = message.get('id') or record.get('uuid') or f'{self.id}-m{self.seq + 1}'
        content = message.get('content')
        if isinstance(content, str):
            content = [{'type': 'text', 'text': content}]
        for block in content if isinstance(content, list) else []:
            if not isinstance(block, dict):
                continue
            kind = block.get('type')
            if kind == 'text' and block.get('text', '').strip():
                self.ensure_run(record)
                self.message('assistant', self.block_id(message_id, ''), block['text'])
            elif kind == 'thinking' and block.get('thinking', '').strip():
                # Only the plain summary; the signature never leaves the log.
                self.ensure_run(record)
                rid = self.block_id(message_id, 'reasoning')
                self.emit('ReasoningMessageStart', messageId=rid)
                self.emit('ReasoningMessageContent', messageId=rid, delta=block['thinking'])
                self.emit('ReasoningMessageEnd', messageId=rid)
            elif kind == 'tool_use' and isinstance(block.get('id'), str):
                self.ensure_run(record)
                self.tool_use(message_id, block)

    def block_id(self, message_id, suffix):
        # One API message is split over several lines; later blocks of the same
        # kind need their own id, still derived from message.id.
        key = (message_id, suffix)
        n = self.blocks.get(key, 0)
        self.blocks[key] = n + 1
        base = f'{message_id}.{suffix}' if suffix else message_id
        return base if n == 0 else f'{base}.{n}'

    def tool_use(self, message_id, block):
        call_id, name = block['id'], str(block.get('name', ''))
        args = block.get('input') if isinstance(block.get('input'), dict) else {}
        self.calls[call_id] = name
        self.translator.call_threads[call_id] = self.id
        self.emit('ToolCallStart', toolCallId=call_id, toolCallName=name, title=title_of(name, args),
                  kind=kind_of(name), parentMessageId=message_id)
        self.emit('ToolCallArgs', toolCallId=call_id,
                  delta=json.dumps(args, ensure_ascii=False, separators=(',', ':')))
        self.emit('ToolCallEnd', toolCallId=call_id)

    def result(self, record, block):
        call_id = block.get('tool_use_id')
        name = self.calls.pop(call_id, None)
        if name is None:
            return  # a result with no call in this thread would be rejected downstream
        content = block.get('content')
        side = record.get('toolUseResult') if isinstance(record.get('toolUseResult'), dict) else {}
        text = result_text(content)
        status = 'failed' if block.get('is_error') else 'ok'
        if side.get('interrupted') is True or (block.get('is_error') and text.startswith(INTERRUPT_MARKERS)):
            status = 'interrupted'
            self.run_interrupted = True
        fields = {'mediaType': 'text/plain'}
        image = sole_image(content)
        if name == 'Bash':
            fields['mediaType'] = 'text/x-ansi'
        elif side.get('structuredPatch'):
            path = side.get('filePath', '')
            fields = {'mediaType': 'text/x-diff',
                      'structured': {'filePath': path, 'hunks': side['structuredPatch']}}
            text = unified_diff(path, side['structuredPatch'])
        elif image:
            fields['mediaType'], text = image
        text, truncated = self.spilled(text) or cap(text, f'{self.source.name}:{self.line}')
        self.emit('ToolCallResult', toolCallId=call_id, messageId=f'{call_id}-result', status=status,
                  **fields, content=text, truncated=truncated)

    def spilled(self, text):
        match = PERSISTED.search(text)
        if not match:
            return None
        spill = Path(match.group(3))
        local = self.translator.session_dir / 'tool-results' / spill.name
        ref = f'{local.parent.parent.name}/tool-results/{spill.name}'
        if local.is_file():
            body = local.read_bytes()
            return cap(body.decode('utf-8', 'replace'), ref)
        # The spill file is gone: keep the preview and say how big it was.
        return text, {'totalBytes': int(float(match.group(1)) * UNITS[match.group(2)]), 'ref': ref}

    def close(self):
        for call_id in list(self.calls):
            self.calls.pop(call_id)
            self.run_interrupted = True
            self.emit('ToolCallResult', toolCallId=call_id, messageId=f'{call_id}-result', status='interrupted',
                      mediaType='text/plain', content='', truncated=None)
        self.finish_run()


class Source:
    """A transcript file read by byte offset, so it can be tailed."""

    def __init__(self, path, thread):
        self.path, self.thread, self.offset, self.pending = path, thread, 0, b''

    def drain(self):
        with self.path.open('rb') as handle:
            handle.seek(self.offset)
            data = handle.read()
        self.offset += len(data)
        lines = (self.pending + data).split(b'\n')
        self.pending = lines.pop()  # an unterminated line may still be being written
        for raw in lines:
            self.thread.line += 1
            try:
                record = json.loads(raw)
            except ValueError:
                continue
            if isinstance(record, dict):
                self.thread.record(record)


class Translator:
    def __init__(self, transcript, out):
        self.transcript, self.out = transcript, out
        self.session_dir = transcript.with_suffix('')
        self.call_threads = {}
        self.main = Thread(self, transcript.stem, transcript)
        self.sources = [Source(transcript, self.main)]
        self.known = {transcript}
        self.write({'type': 'Hello', 'contract': CONTRACT})

    def write(self, event):
        self.out.write(json.dumps(event, ensure_ascii=False, separators=(',', ':')) + '\n')

    def discover(self, final):
        """Add subagent files whose parent call is known (or, at the end, all of them)."""
        found = sorted(p for p in (self.session_dir / 'subagents').glob('agent-*.jsonl') if p not in self.known)
        added = []
        for path in found:
            meta_path = path.with_name(path.stem + '.meta.json')
            meta = {}
            if meta_path.is_file():
                try:
                    meta = json.loads(meta_path.read_text())
                except ValueError:
                    meta = {}
            call_id = meta.get('toolUseId')
            parent_thread = self.call_threads.get(call_id)
            if call_id and parent_thread is None and not final:
                continue  # the spawning call has not been read yet
            parent = {'threadId': parent_thread or self.main.id, 'toolCallId': call_id} if call_id else None
            self.known.add(path)
            source = Source(path, Thread(self, path.stem, path, parent))
            self.sources.append(source)
            added.append(source)
        return added

    def step(self, final=False):
        for source in self.sources:
            source.drain()
        # Draining a new subagent can reveal the parent call of a nested one.
        while True:
            added = self.discover(final=False)
            if not added and final:
                added = self.discover(final=True)
            if not added:
                break
            for source in added:
                source.drain()
        self.out.flush()

    def finish(self):
        self.step(final=True)
        for source in self.sources:
            source.thread.close()
        self.out.flush()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('transcript', type=Path, help='Claude Code session .jsonl')
    parser.add_argument('-o', '--output', type=Path, help='write events here instead of stdout')
    parser.add_argument('--follow', action='store_true', help='keep tailing the transcript and append new events')
    args = parser.parse_args(argv)
    out = args.output.open('w', encoding='utf-8', newline='\n') if args.output else sys.stdout
    translator = Translator(args.transcript, out)
    if not args.follow:
        translator.finish()
        return 0
    # Following never ends the session itself, so open calls stay pending.
    try:
        while True:
            translator.step()
            time.sleep(POLL_SECONDS)
    except KeyboardInterrupt:
        return 0


if __name__ == '__main__':
    sys.exit(main())
