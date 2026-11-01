#!/usr/bin/env python3
"""Golden, --follow and ordering tests for claude-to-events.py.

Run with --update to rewrite the goldens after a deliberate change.
"""
import collections
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

TOOL = Path(__file__).with_name('claude-to-events.py')
FIXTURES = Path(__file__).resolve().parent.parent / 'fixtures' / 'claude-code'
spec = importlib.util.spec_from_file_location('claude_to_events', TOOL)
translator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(translator)


def transcript(case):
    return next(case.glob('*.jsonl'))


def translate(path, final=True):
    out = io.StringIO()
    run = translator.Translator(path, out)
    if final:
        run.finish()
    else:
        run.step()
    return out.getvalue()


def events(text):
    return [json.loads(line) for line in text.splitlines()]


def by_thread(items):
    threads = collections.defaultdict(list)
    for event in items:
        if 'threadId' in event:
            threads[event['threadId']].append(event)
    return dict(threads)


def wait_for(predicate, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.05)
    return predicate()


class GoldenTests(unittest.TestCase):
    def test_each_fixture_matches_its_golden(self):
        cases = sorted(p for p in FIXTURES.iterdir() if p.is_dir())
        self.assertEqual(len(cases), 8)
        for case in cases:
            with self.subTest(case.name):
                golden = FIXTURES / f'{case.name}.events.jsonl'
                self.assertEqual(translate(transcript(case)), golden.read_text(encoding='utf-8'))

    def test_seq_counts_up_from_one_within_each_thread(self):
        for golden in FIXTURES.glob('*.events.jsonl'):
            lines = events(golden.read_text(encoding='utf-8'))
            self.assertEqual(lines[0], {'type': 'Hello', 'contract': 1})
            for thread, items in by_thread(lines).items():
                with self.subTest(golden.name, thread=thread):
                    self.assertEqual([e['seq'] for e in items], list(range(1, len(items) + 1)))

    def test_thinking_signatures_never_leave_the_transcript(self):
        for golden in FIXTURES.glob('*.events.jsonl'):
            self.assertNotIn('c2lnbmF0dXJl', golden.read_text(encoding='utf-8'))

    def test_inline_output_over_the_cap_points_back_at_its_line(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'session.jsonl'
            body = 'é' * translator.OUTPUT_CAP
            records = [
                {'type': 'user', 'uuid': 'p', 'message': {'role': 'user', 'content': 'go'}},
                {'type': 'assistant', 'uuid': 'a', 'message': {'id': 'm', 'role': 'assistant', 'content': [
                    {'type': 'tool_use', 'id': 'c', 'name': 'Read', 'input': {'file_path': 'big.txt'}}]}},
                {'type': 'user', 'uuid': 'r', 'message': {'role': 'user', 'content': [
                    {'type': 'tool_result', 'tool_use_id': 'c', 'content': body}]}},
            ]
            path.write_text(''.join(json.dumps(r) + '\n' for r in records), encoding='utf-8')
            result = next(e for e in events(translate(path)) if e['type'] == 'ToolCallResult')
        self.assertEqual(result['truncated'], {'totalBytes': len(body.encode()), 'ref': 'session.jsonl:3'})
        self.assertLessEqual(len(result['content'].encode()), translator.OUTPUT_CAP)
        self.assertEqual(result['content'], 'é' * (translator.OUTPUT_CAP // 2))

    def test_an_edit_too_large_for_the_cap_drops_its_hunks(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'session.jsonl'
            hunk = {'oldStart': 1, 'oldLines': 0, 'newStart': 1, 'newLines': 1,
                    'lines': ['+' + 'x' * translator.OUTPUT_CAP]}
            records = [
                {'type': 'user', 'uuid': 'p', 'message': {'role': 'user', 'content': 'go'}},
                {'type': 'assistant', 'uuid': 'a', 'message': {'id': 'm', 'role': 'assistant', 'content': [
                    {'type': 'tool_use', 'id': 'c', 'name': 'Write', 'input': {'file_path': 'big.txt'}}]}},
                {'type': 'user', 'uuid': 'r', 'toolUseResult': {'filePath': 'big.txt', 'structuredPatch': [hunk]},
                 'message': {'role': 'user', 'content': [{'type': 'tool_result', 'tool_use_id': 'c', 'content': 'ok'}]}},
            ]
            path.write_text(''.join(json.dumps(r) + '\n' for r in records), encoding='utf-8')
            result = next(e for e in events(translate(path)) if e['type'] == 'ToolCallResult')
        self.assertEqual(result['mediaType'], 'text/x-diff')
        self.assertNotIn('structured', result)
        self.assertEqual(result['truncated']['ref'], 'session.jsonl:3')


class FollowTests(unittest.TestCase):
    def test_follow_appends_events_as_the_transcripts_grow(self):
        source = FIXTURES / 'subagent'
        with tempfile.TemporaryDirectory() as tmp:
            case = Path(tmp) / 'subagent'
            shutil.copytree(source, case)
            files = [transcript(case)] + sorted((case / transcript(case).stem / 'subagents').glob('*.jsonl'))
            lines = {f: f.read_bytes().splitlines(keepends=True) for f in files}
            for f in files:
                f.write_bytes(b'')
            # Main up to and including the Agent call, then the child, the
            # grandchild, and the rest of the main transcript.
            schedule = [(files[0], i) for i in range(2)] + [(files[1], i) for i in range(len(lines[files[1]]))] \
                + [(files[2], i) for i in range(len(lines[files[2]]))] \
                + [(files[0], i) for i in range(2, len(lines[files[0]]))]
            out = case / 'out.events.jsonl'
            process = subprocess.Popen([sys.executable, str(TOOL), str(files[0]), '-o', str(out), '--follow'])
            try:
                hello = '{"type":"Hello","contract":1}\n'
                self.assertTrue(wait_for(lambda: out.exists() and out.read_text(encoding='utf-8') == hello))
                seen = hello
                for path, index in schedule:
                    line = lines[path][index]
                    with path.open('ab') as handle:  # half a line first: nothing may come of it
                        handle.write(line[:len(line) // 2])
                    time.sleep(translator.POLL_SECONDS * 2)
                    self.assertEqual(out.read_text(encoding='utf-8'), seen)
                    with path.open('ab') as handle:
                        handle.write(line[len(line) // 2:])
                    expected = by_thread(events(translate(files[0], final=False)))
                    self.assertTrue(wait_for(lambda: by_thread(events(out.read_text(encoding='utf-8'))) == expected),
                                    f'follow output lagged after {path.name} line {index + 1}')
                    current = out.read_text(encoding='utf-8')
                    self.assertTrue(current.startswith(seen), 'follow rewrote earlier output')
                    seen = current
            finally:
                process.terminate()
                process.wait(timeout=10)
        # Following never declares the session over, so the runs stay open.
        self.assertNotIn('RunFinished', seen)
        self.assertEqual(
            {t: [e['seq'] for e in items] for t, items in by_thread(events(seen)).items()},
            {t: list(range(1, len(items) + 1)) for t, items in by_thread(events(seen)).items()})

    def test_a_subagent_waits_for_its_meta_file_while_following(self):
        with tempfile.TemporaryDirectory() as tmp:
            case = Path(tmp) / 'subagent'
            shutil.copytree(FIXTURES / 'subagent', case)
            main = transcript(case)
            subagents = main.with_suffix('') / 'subagents'
            metas = {p: p.read_bytes() for p in subagents.glob('*.meta.json')}
            for meta in metas:
                meta.unlink()
            out = io.StringIO()
            run = translator.Translator(main, out)
            run.step()
            self.assertEqual(set(by_thread(events(out.getvalue()))), {main.stem})
            for meta, body in metas.items():
                meta.write_bytes(body)
            run.step()
        starts = {e['threadId']: e.get('parent') for e in events(out.getvalue()) if e['type'] == 'RunStarted'}
        self.assertEqual(starts['agent-a1b2c3'], {'threadId': main.stem, 'toolCallId': 'toolu_spawn'})
        self.assertEqual(starts['agent-d4e5f6'], {'threadId': 'agent-a1b2c3', 'toolCallId': 'toolu_child_spawn'})


def update():
    for case in sorted(p for p in FIXTURES.iterdir() if p.is_dir()):
        (FIXTURES / f'{case.name}.events.jsonl').write_text(translate(transcript(case)), encoding='utf-8', newline='\n')


if __name__ == '__main__':
    if '--update' in sys.argv:
        update()
    else:
        unittest.main()
