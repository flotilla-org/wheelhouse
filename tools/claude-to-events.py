#!/usr/bin/env python3
"""Translate a Claude Code session transcript into conversation events (#615).

Interim and throwaway: Flotilla's normaliser (flotilla-org/flotilla#4206)
replaces it. The wire format is the one fixed on #615 and decided on #548.
"""
import argparse
import collections
import json
from pathlib import Path
import re
import sys
import time

CONTRACT = 2
OUTPUT_CAP = 64 * 1024
# Base64 can't be cut and still decode, so an image is whole or replaced by a marker.
IMAGE_CAP = 4 * 1024 * 1024
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
LEADING_TAG = re.compile(r'\s*<([a-z][\w-]*)>')
COMMAND_TAGS = {'command-name', 'command-message'}
NOTICE_TAGS = {'system-reminder', 'ci-monitor-event', 'side-sessions-event', 'local-command-caveat',
               'local-command-stdout', 'local-command-stderr', 'cross-session-message', 'agent-message'}
SKILL_BASE = 'Base directory for this skill:'
# The classes that start a turn when they arrive on their own (no tool results).
OPENERS = {'prompt', 'sdk', 'notification', 'peer', 'command', 'image'}


def kind_of(name):
    if name.startswith('mcp__'):
        return 'mcp'
    return KINDS.get(name, 'other')


def shorten(text):
    text = ' '.join(text.split())
    return text if len(text) <= TITLE_CAP else text[:TITLE_CAP - 1] + '…'


def title_of(name, args):
    # A Bash call's model-written description reads better than its command (#667).
    for key in ('description', 'command') if name == 'Bash' else ():
        if isinstance(args.get(key), str) and args[key].strip():
            return shorten(args[key])
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


def image_of(block):
    source = block.get('source') or {}
    if block.get('type') == 'image' and source.get('type') == 'base64' and isinstance(source.get('data'), str):
        return source.get('media_type', 'image/png'), source['data']
    return None


def images_of(content):
    blocks = content if isinstance(content, list) else []
    return [image for image in (image_of(b) for b in blocks if isinstance(b, dict)) if image]


def image_cap(data, ref):
    if len(data) <= IMAGE_CAP:
        return data, None
    return '', {'totalBytes': len(data), 'ref': ref}


def leading_tag(text):
    match = LEADING_TAG.match(text)
    return match.group(1) if match else None


def tag_value(text, tag):
    match = re.search(rf'<{tag}>(.*?)</{tag}>', text, re.S)
    return match.group(1).strip() if match else None


def classify(text, record, origin, brief=False):
    """The class of one user-role text block: origin first, then isMeta, then the leading tag.

    A typed prompt can share a record with a reminder, so this is per block.
    """
    kind = origin.get('kind')
    tag = leading_tag(text)
    if record.get('isCompactSummary'):
        return 'compaction'
    if text.lstrip().startswith(INTERRUPT_MARKERS):
        return 'interrupt'
    if kind == 'task-notification' or tag == 'task-notification':
        return 'notification'
    # A non-human sender outranks the tags: its text is attributed to it whatever it holds.
    if kind and kind != 'human':
        return 'peer'
    if tag in COMMAND_TAGS:
        return 'command'
    if record.get('isMeta'):
        return 'expansion' if text.startswith(SKILL_BASE) else 'notice'
    if tag in NOTICE_TAGS:
        return 'notice'
    # Flotilla's one-shot probes run through the SDK and stamp no origin.
    if not kind and record.get('entrypoint') == 'sdk-cli':
        return 'sdk'
    return 'brief' if brief else 'prompt'


def attribution(origin, **extra):
    # The peer origin repeats the message as `body`; the message carries it once.
    fields = {k: v for k, v in origin.items() if k != 'body' and isinstance(v, (str, int, float, bool))}
    return {**fields, **{k: v for k, v in extra.items() if v is not None}}


class Thread:
    """One threadId: the main session or one subagent."""

    def __init__(self, translator, thread_id, source, parent=None):
        self.translator, self.id, self.source, self.parent = translator, thread_id, source, parent
        self.seq = 0
        self.run = None
        self.run_cancelled = False
        self.calls = {}  # open toolCallId -> name
        self.blocks = {}  # (message.id, kind) -> blocks of that kind seen so far
        self.line = 0
        self.at = {'log': str(source)}  # the source back-reference of the record being read
        self.briefed = parent is None
        # A compaction boundary waits for the summary record written right after it.
        self.compaction = None
        self.counts = collections.Counter()

    def emit(self, event_type, **fields):
        self.seq += 1
        self.translator.write({'type': event_type, 'threadId': self.id, 'seq': self.seq, **fields,
                               'source': self.at})

    def start_run(self, run_id):
        self.finish_run()
        self.run, self.run_cancelled = run_id, False
        extra = {'parent': self.parent} if self.parent else {}
        self.emit('RunStarted', runId=run_id, **extra)

    def finish_run(self):
        if self.run is not None:
            # No outcome is success.
            extra = {'outcome': {'type': 'cancelled'}} if self.run_cancelled else {}
            self.emit('RunFinished', runId=self.run, **extra)
            self.run = None

    def ensure_run(self, record):
        if self.run is None:
            self.start_run(record.get('uuid') or f'{self.id}-run{self.seq + 1}')

    def message(self, role, message_id, text, origin=None):
        extra = {'origin': origin} if origin else {}
        self.emit('TextMessageStart', messageId=message_id, role=role, **extra)
        self.emit('TextMessageContent', messageId=message_id, delta=text)
        self.emit('TextMessageEnd', messageId=message_id)

    def record(self, record):
        kind = record.get('type')
        if self.compaction and not (kind == 'user' and record.get('isCompactSummary')):
            self.flush_compaction()
        uuid = {'record': record['uuid']} if isinstance(record.get('uuid'), str) else {}
        self.at = {'log': str(self.source), **uuid, 'line': self.line}
        if kind == 'user':
            self.user(record)
        elif kind == 'assistant':
            self.assistant(record)
        elif kind == 'attachment' and (record.get('attachment') or {}).get('type') == 'queued_command':
            self.queued(record, record['attachment'])
        elif kind == 'system' and record.get('subtype') == 'compact_boundary':
            meta = record.get('compactMetadata') or {}
            self.compaction = ({k: meta[k] for k in ('trigger', 'preTokens') if k in meta}, record, self.at)

    def flush_compaction(self, summary=None):
        value, record, at = self.compaction
        self.compaction, here, self.at = None, self.at, at
        self.ensure_run(record)
        self.emit('Custom', name='compaction', value={**value, **({'summary': summary} if summary else {})})
        self.at = here

    def user(self, record):
        content = (record.get('message') or {}).get('content')
        if isinstance(content, str):
            content = [{'type': 'text', 'text': content}]
        if not isinstance(content, list):
            return
        results = [b for b in content if isinstance(b, dict) and b.get('type') == 'tool_result']
        for block in results:
            self.result(record, block)
        origin = record.get('origin') if isinstance(record.get('origin'), dict) else {}
        blocks = self.classified(content, record, origin)
        if not results and not record.get('isMeta') and any(c in OPENERS for c, _ in blocks):
            self.finish_run()
        self.content(blocks, record, origin)

    def queued(self, record, attachment):
        # Typed (or delivered) while a turn ran, and folded into that turn: it never opens one.
        prompt = attachment.get('prompt')
        content = [{'type': 'text', 'text': prompt}] if isinstance(prompt, str) else prompt
        if not isinstance(content, list):
            return
        origin = attachment.get('origin') if isinstance(attachment.get('origin'), dict) else {}
        self.content(self.classified(content, record, origin), record, origin)

    def classified(self, content, record, origin):
        blocks = []
        for block in content:
            if not isinstance(block, dict):
                continue
            if block.get('type') == 'text' and block.get('text', '').strip():
                blocks.append((classify(block['text'], record, origin, brief=not self.briefed), block))
            elif image_of(block):
                blocks.append(('image', block))
        # A reminder can come before the brief; only the brief itself ends the wait for it.
        if any(c == 'brief' for c, _ in blocks):
            self.briefed = True
        return blocks

    def content(self, blocks, record, origin):
        uuid = record.get('uuid') or f'{self.id}-m{self.seq + 1}'
        for kind, block in blocks:
            self.counts[kind] += 1
            if kind == 'compaction' and self.compaction:
                self.flush_compaction(block['text'])
                continue
            self.ensure_run(record)
            if kind == 'image':
                media_type, data = image_of(block)
                data, truncated = image_cap(data, f'{self.source.name}:{self.line}')
                self.emit('Custom', name='image', value={'messageId': uuid, 'mediaType': media_type, 'data': data,
                                                         'truncated': truncated})
                continue
            text, message_id = block['text'], self.block_id(uuid, '')
            if kind in ('prompt', 'brief'):
                self.message('user', message_id, text)
            elif kind == 'sdk':
                self.message('system', message_id, text, {'kind': 'sdk', 'name': 'Flotilla'})
            elif kind == 'notification':
                note = origin if origin.get('kind') == 'task-notification' else {'kind': 'task-notification'}
                self.message('system', message_id, text, attribution(note, toolCallId=tag_value(text, 'tool-use-id')))
            elif kind == 'peer':
                self.message('system', message_id, text, attribution(origin))
            elif kind in ('notice', 'interrupt'):
                tag = 'interrupt' if kind == 'interrupt' else leading_tag(text)
                self.message('system', message_id, text, attribution({'kind': 'harness'}, tag=tag))
                self.run_cancelled |= kind == 'interrupt'
            elif kind == 'command':
                name = tag_value(text, 'command-name') or tag_value(text, 'command-message') or ''
                args = tag_value(text, 'command-args')
                self.emit('Custom', name='command', value={'name': name.lstrip('/'), 'args': args, 'body': text})
            elif kind == 'expansion':
                path = text[len(SKILL_BASE):].split('\n', 1)[0].strip()
                call = {'toolCallId': record['sourceToolUseID']} if record.get('sourceToolUseID') else {}
                self.emit('Custom', name='command', value={'name': Path(path).name, **call, 'body': text})
            elif kind == 'compaction':  # a summary with no boundary before it
                self.emit('Custom', name='compaction', value={'summary': text})

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
            status = 'cancelled'
            self.run_cancelled = True
        fields = {'mediaType': 'text/plain'}
        images = images_of(content)
        image = images[0] if len(images) == 1 and not text else None
        ref = f'{self.source.name}:{self.line}'
        if image:
            fields['mediaType'], text = image
        elif name == 'Bash':
            fields['mediaType'] = 'text/x-ansi'
        elif side.get('structuredPatch'):
            path = side.get('filePath', '')
            text = unified_diff(path, side['structuredPatch'])
            fields = {'mediaType': 'text/x-diff'}
            # The hunks repeat the diff, so they go only when it fits under the cap.
            if len(text.encode('utf-8')) <= OUTPUT_CAP:
                fields['structured'] = {'filePath': path, 'hunks': side['structuredPatch']}
        text, truncated = image_cap(text, ref) if image else self.spilled(text) or cap(text, ref)
        self.emit('ToolCallResult', toolCallId=call_id, messageId=f'{call_id}-result', status=status,
                  **fields, content=text, truncated=truncated)
        # A result that mixes text and images keeps the text as its content.
        for media_type, data in images if not image else []:
            data, truncated = image_cap(data, ref)
            self.emit('Custom', name='image', value={'toolCallId': call_id, 'mediaType': media_type, 'data': data,
                                                     'truncated': truncated})

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
        if self.compaction:
            self.flush_compaction()
        self.at = {'log': str(self.source)}
        for call_id in list(self.calls):
            self.calls.pop(call_id)
            self.run_cancelled = True
            self.emit('ToolCallResult', toolCallId=call_id, messageId=f'{call_id}-result', status='cancelled',
                      mediaType='text/plain', content='', truncated=None)
        self.finish_run()


class Source:
    """A transcript file read by byte offset, so it can be tailed."""

    def __init__(self, path, thread):
        self.path, self.thread, self.offset, self.pending = path, thread, 0, b''

    def drain(self):
        # Claude Code only appends; a transcript that shrinks or is replaced
        # is not detected, and reading resumes at the old offset.
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
                print(f'{self.path.name}:{self.thread.line}: skipping a line that is not JSON', file=sys.stderr)
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
            try:
                meta = json.loads(meta_path.read_text())
            except (OSError, ValueError):
                meta = None
            if not isinstance(meta, dict):
                if not final:
                    continue  # meta.json may not be written yet
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
    parser.add_argument('--counts', action='store_true',
                        help='print how many user-role blocks fell in each class, as JSON on stderr')
    args = parser.parse_args(argv)
    out = args.output.open('w', encoding='utf-8', newline='\n') if args.output else sys.stdout
    translator = Translator(args.transcript, out)
    if not args.follow:
        translator.finish()
        if args.counts:
            counts = {'main': collections.Counter(), 'subagents': collections.Counter()}
            for source in translator.sources:
                counts['main' if source.thread is translator.main else 'subagents'].update(source.thread.counts)
            print(json.dumps(counts, sort_keys=True), file=sys.stderr)
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
