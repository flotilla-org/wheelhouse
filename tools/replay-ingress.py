#!/usr/bin/env python3
"""Replay accepted metadata into the real Andamento ABI, or redact a capture."""
import argparse
import ctypes as C
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read_records(paths):
    records = []
    for path in paths:
        for line in Path(path).read_text(encoding='utf-8').splitlines():
            if line.strip():
                records.append(json.loads(line))
    # Responses can finish out of order. Sequence numbers describe reception.
    if records and all('sequence' in r for r in records):
        records.sort(key=lambda r: (r.get('recording_id', ''), r['sequence']))
    return records

# Structural identities and lifecycle facts must survive. All other text facts
# are hashed, including recipes/URLs which can embed private paths or hosts.
PUBLIC_TEXT = {'entity.kind', 'entity.id', 'flotilla.project', 'flotilla.convoy',
               'flotilla.vessel', 'flotilla.role', 'flotilla.convoy.phase',
               'flotilla.role.phase', 'flotilla.role.state', 'status.phase',
               'status.state', 'flotilla.role.name', 'flotilla.role.presents_as',
               'flotilla.forge', 'workspace.primary.target', 'workspace.primary.state',
               'flotilla.change_request.state', 'flotilla.change_request.readiness',
               'flotilla.change_request.checks', 'flotilla.change_request.mergeable',
               'flotilla.change_request.review_decision', 'flotilla.issue.state',
               'forge.kind', 'forge.state', 'action.primary.target'}

def digest(value):
    if not isinstance(value, str):
        value = json.dumps(value, sort_keys=True)
    return 'sha256:' + hashlib.sha256(value.encode()).hexdigest()

def hash_strings(value):
    if isinstance(value, str):
        return digest(value)
    if isinstance(value, list):
        return [hash_strings(item) for item in value]
    if isinstance(value, dict):
        return {key: hash_strings(item) for key, item in value.items()}
    return value

def redact_value(value):
    if not isinstance(value, dict):
        return hash_strings(value)
    kind = value.get('type')
    if kind == 'text':
        value['value'] = digest(value.get('value'))
    elif kind == 'string-list' and isinstance(value.get('value'), list):
        value['value'] = [digest(item) for item in value['value']]
    elif kind == 'group-path' and isinstance(value.get('value'), list):
        for segment in value['value']:
            if not isinstance(segment, dict):
                continue
            for key in list(segment):
                if key == 'value':
                    segment[key] = redact_value(segment[key])
                elif key != 'key':
                    segment[key] = hash_strings(segment[key])
    elif kind not in ('bool', 'integer', 'entity-refs'):
        # Future value kinds must not provide an escape hatch for private text.
        value['value'] = hash_strings(value.get('value'))
    for key in list(value):
        if key not in ('type', 'value'):
            value[key] = hash_strings(value[key])
    return value


def redact_patch(patch):
    patch = json.loads(json.dumps(patch))
    updates = patch.get('set', {})
    if isinstance(updates, dict):
        for key, fact in updates.items():
            if not isinstance(fact, dict):
                updates[key] = hash_strings(fact)
                continue
            if key not in PUBLIC_TEXT:
                fact['value'] = redact_value(fact.get('value', {}))
            for attribute in list(fact):
                if attribute != 'value':
                    fact[attribute] = hash_strings(fact[attribute])
    else:
        patch['set'] = hash_strings(updates)
    for key in list(patch):
        if key not in ('type', 'target', 'source_id', 'set', 'unset'):
            patch[key] = hash_strings(patch[key])
    return patch


def redact(records):
    for entry in records:
        entry = dict(entry)
        if 'target' in entry and 'set' in entry:
            yield redact_patch(entry)
            continue
        patch = entry.get('body')
        if isinstance(patch, dict) and patch.get('type') == 'metadata-patch':
            entry['body'] = redact_patch(patch)
            if entry.get('body_utf8') is not None:
                entry['body_utf8'] = json.dumps(entry['body'], separators=(',', ':'))
        else:
            # Invalid bodies can contain arbitrary secrets and aren't replayed.
            entry['body'] = None
            entry['body_utf8'] = None
        if entry.get('producer') is not None:
            # source_id is an identity, deliberately preserved.
            entry['producer'] = patch.get('source_id') if isinstance(patch, dict) else None
        if entry.get('path') not in ('/v1/metadata/patch', '/v1/health', '/v1/observed/workdirs'):
            entry['path'] = digest(entry['path'])
        yield entry

def native_api(path):
    spec = importlib.util.spec_from_file_location('native_sidebar', ROOT / 'tools/test-native-sidebar.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module, module.load(path)

def snapshot_nodes(abi, lib, core):
    error = C.c_char_p()
    snapshot = lib.andamento_snapshot_acquire(core, C.byref(error))
    if not snapshot:
        raise ValueError(error.value)
    try:
        nodes = []
        for i in range(lib.andamento_snapshot_node_count(snapshot)):
            node = abi.Node()
            if not lib.andamento_snapshot_node(snapshot, i, C.byref(node)):
                raise ValueError('node unavailable')
            fields = []
            for j in range(node.first_field, node.first_field + node.field_count):
                field = abi.Field()
                lib.andamento_snapshot_field(snapshot, j, C.byref(field))
                fields.append(field.text.string())
            if not node.is_section:
                nodes.append(dict(kind=node.entity_kind.string(), id=node.entity_id.string(),
                                  label=node.label.string(), state=node.state, fields=fields))
        return nodes
    finally:
        lib.andamento_snapshot_release(snapshot)

def replay(records, library, template=ROOT / 'data/sidebar/daily-driver.kdl'):
    abi, lib = native_api(library)
    config = Path(template).read_bytes()
    error = C.c_char_p()
    core = lib.andamento_create(config, len(config), C.byref(error))
    if not core:
        raise ValueError(error.value)
    session = None
    try:
        for index, entry in enumerate(records):
            current = entry.get('recording_id')
            if current is not None and current != session:
                if session is not None:
                    lib.andamento_destroy(core)
                    core = lib.andamento_create(config, len(config), C.byref(error))
                    if not core:
                        raise ValueError(error.value)
                session = current
            fixture = 'target' in entry and 'set' in entry
            if not fixture and (entry.get('path') != '/v1/metadata/patch' or entry.get('status') != 204):
                continue
            patch = dict(entry, type='metadata-patch') if fixture else entry['body']
            body = json.dumps(patch) if fixture else entry.get('body_utf8') or json.dumps(patch)
            now = 0 if fixture else entry['received_ms']
            if not lib.andamento_apply_patch_json(core, now, abi.Text.of(body), C.byref(error)):
                raise ValueError(f'patch {index}: {error.value}')
            if not lib.andamento_tick(core, now, C.byref(error)):
                raise ValueError(f'tick {index}: {error.value}')
            nodes = snapshot_nodes(abi, lib, core)
            yield dict(patch=index, recording_id=session, status=entry.get('status', 204),
                       source_id=patch.get('source_id'), received_ms=now, nodes=nodes)
    finally:
        if core:
            lib.andamento_destroy(core)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('recordings', nargs='+', type=Path)
    parser.add_argument('--library', type=Path, help='Andamento shared library (required for replay)')
    parser.add_argument('--template', type=Path, default=ROOT / 'data/sidebar/daily-driver.kdl')
    parser.add_argument('--redact', action='store_true', help='write redacted JSONL to stdout; no ABI needed')
    args = parser.parse_args()
    records = read_records(args.recordings)
    if not args.redact and not args.library:
        parser.error('--library is required for replay')
    for result in redact(records) if args.redact else replay(records, args.library, args.template):
        print(json.dumps(result, ensure_ascii=False))

if __name__ == '__main__':
    main()
