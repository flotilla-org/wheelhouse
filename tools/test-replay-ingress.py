#!/usr/bin/env python3
"""Generated redaction invariants; native replay is tested by test-andamento-ingress.py."""
import importlib.util
import itertools
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('replay_ingress', Path(__file__).with_name('replay-ingress.py'))
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)

class RedactionTests(unittest.TestCase):
    def test_sensitive_values_disappear_without_changing_identities_or_order(self):
        # Issue #223: generate empty, Unicode, Windows/Unix paths, hosts, and
        # labels in every supported string-bearing metadata value variant.
        secrets = ['', '/home/private/work', r'C:\Users\private\repo', 'private.example.net', '秘密 label']
        keys = ['checkout.path', 'flotilla.vessel.host', 'display.label', 'status.detail', 'action.primary.recipe']
        for key, secret in itertools.product(keys, secrets):
            patch = {'type': 'metadata-patch', 'source_id': 'source-id',
                     'target': {'kind': 'entity', 'value': {'kind': 'role', 'id': 'resource-id'}},
                     'set': {key: {'value': {'type': 'text', 'value': secret}},
                             'private.list': {'value': {'type': 'string-list', 'value': [secret, secret]}},
                             'private.group': {'value': {'type': 'group-path', 'value': [
                                 {'key': 'group-key', 'label': secret, 'value': {'type': 'text', 'value': secret}}]}},
                             'flotilla.convoy': {'value': {'type': 'text', 'value': 'convoy-id'}},
                             'flotilla.convoy.phase': {'value': {'type': 'text', 'value': 'active'}},
                             'flotilla.role.attempts': {'value': {'type': 'entity-refs', 'value': [
                                 {'kind': 'convoy', 'id': 'first'}, {'kind': 'convoy', 'id': 'second'}]}}},
                     'unset': ['old.key'], 'extension': {'private': secret}}
            records = [dict(sequence=i, path='/v1/metadata/patch', producer='source-id', status=204,
                            body=patch, body_utf8=json.dumps(patch)) for i in range(3)]
            result = list(replay.redact(records))
            self.assertEqual([r['sequence'] for r in result], [0, 1, 2])
            for item in result:
                redacted = item['body']
                self.assertEqual(redacted['source_id'], patch['source_id'])
                self.assertEqual(redacted['target'], patch['target'])
                self.assertEqual(list(redacted['set']), list(patch['set']))
                self.assertEqual(redacted['unset'], patch['unset'])
                self.assertEqual(redacted['extension']['private'], replay.digest(secret))
                for public in ('flotilla.convoy', 'flotilla.convoy.phase', 'flotilla.role.attempts'):
                    self.assertEqual(redacted['set'][public], patch['set'][public])
                self.assertEqual(redacted['set'][key]['value']['value'], replay.digest(secret))
                self.assertEqual(redacted['set']['private.list']['value']['value'], [replay.digest(secret)] * 2)
                group = redacted['set']['private.group']['value']['value'][0]
                self.assertEqual(group['key'], 'group-key')
                self.assertEqual(group['label'], replay.digest(secret))
                self.assertEqual(group['value']['value'], replay.digest(secret))
                self.assertEqual(json.loads(item['body_utf8']), redacted)
                if secret:
                    self.assertNotIn(secret, json.dumps(item, ensure_ascii=False))
            self.assertEqual(patch['set'][key]['value']['value'], secret) # input untouched

    def test_invalid_body_is_removed_and_unknown_request_path_is_hashed(self):
        # Invalid requests cannot contribute sidebar state and may contain secrets.
        result = list(replay.redact([{'path': '/private/host', 'body': {'secret': '/home/private'},
                                     'body_utf8': 'broken private body', 'status': 400}]))[0]
        self.assertIsNone(result['body'])
        self.assertIsNone(result['body_utf8'])
        self.assertEqual(result['path'], replay.digest('/private/host'))

    def test_rotated_records_restore_receive_order_across_restarts(self):
        # Issue #223: reverse completion and file order must not reorder patches.
        records = [dict(recording_id=session, sequence=i, timestamp_ms=10, received_ms=2)
                   for session in ('001', '002') for i in range(4)]
        with tempfile.TemporaryDirectory() as directory:
            files = []
            for part, entries in enumerate((records[::2], records[1::2])):
                path = Path(directory) / str(part)
                path.write_text('\n'.join(json.dumps(r) for r in reversed(entries)))
                files.append(path)
            self.assertEqual(replay.read_records(reversed(files)), records)
        self.assertEqual(list(replay.redact([])), [])

if __name__ == '__main__':
    unittest.main()
