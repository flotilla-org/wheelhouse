#!/usr/bin/env python3
"""Check the shipped native template against Andamento's real typed C ABI."""
import ctypes as C
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
U32, U64, Size, Ptr = C.c_uint32, C.c_uint64, C.c_size_t, C.c_void_p

class Text(C.Structure):
    _fields_ = [('data', C.c_char_p), ('len', Size)]
    @classmethod
    def of(cls, value):
        value = value.encode() if isinstance(value, str) else value
        return cls(value, len(value))
    def string(self):
        return C.string_at(self.data, self.len).decode() if self.len else ''

class Node(C.Structure):
    _fields_ = [('parent', Size), ('is_section', U32)] + [(x, Text) for x in
        ['key', 'entity_kind', 'entity_id', 'label', 'layout', 'form']] + [
        ('state', U32), ('workspace_id', U64)] + [(x, U32) for x in
        ['selected', 'openable', 'collapsed', 'pinned']] + [(x, Size) for x in
        ['first_field', 'field_count', 'first_detail', 'detail_count', 'first_control', 'control_count', 'activate', 'toggle']]

class Field(C.Structure):
    _fields_ = [("text", Text), ("class_", U32), ("has_priority", U32), ("priority", C.c_int64)]


class Control(C.Structure):
    _fields_ = [("kind", U32), ("label", Text), ("glyph", Text), ("action", Size),
                ("value_kind", U32), ("checked", U32), ("value", Text)]


class Effect(C.Structure):
    _fields_ = [('kind', U32), ('request_id', U64), ('workspace_id', U64)] + [(x, Text) for x in
        ['entity_kind', 'entity_id', 'name', 'recipe']] + [('has_cwd', U32), ('cwd', Text)]

class Workspace(C.Structure):
    _fields_ = [('id', U64), ('position', Size), ('name', Text), ('selected', U32)]


def load(path):
    lib = C.CDLL(str(path))
    signatures = {
        'create': (Ptr, [C.c_char_p, Size, Ptr]), 'destroy': (None, [Ptr]),
        'configure': (U32, [Ptr, Text, Ptr]),
        'tick': (U32, [Ptr, U64, Ptr]),
        'snapshot_is_current': (U32, [Ptr, Ptr, Ptr]),
        'snapshot_field': (U32, [Ptr, Size, C.POINTER(Field)]),
        'snapshot_node_loop_key': (U32, [Ptr, Size, C.POINTER(Text)]),
        'apply_patch_json': (U32, [Ptr, U64, Text, Ptr]),
        'snapshot_acquire': (Ptr, [Ptr, Ptr]), 'snapshot_release': (None, [Ptr]),
        'snapshot_node_count': (Size, [Ptr]), 'snapshot_node': (U32, [Ptr, Size, C.POINTER(Node)]),
        'snapshot_control': (U32, [Ptr, Size, C.POINTER(Control)]),
        'snapshot_diagnostic_count': (Size, [Ptr]),
        'dispatch': (U32, [Ptr, Ptr, Size, Ptr]), 'effects_take': (Ptr, [Ptr, Ptr]),
        'effects_count': (Size, [Ptr]), 'effects_get': (U32, [Ptr, Size, C.POINTER(Effect)]),
        'effects_release': (None, [Ptr]), 'complete': (U32, [Ptr, U64, U32, U64, Text, Ptr]),
        'observe': (U32, [Ptr, C.POINTER(Workspace), Size, Ptr, Size, Ptr]),
    }
    for name, (result, args) in signatures.items():
        fn = getattr(lib, 'andamento_' + name)
        fn.restype, fn.argtypes = result, args
    return lib


def patch(kind, identity, **facts):
    facts = {'entity.kind': kind, 'entity.id': identity, 'display.label': identity, **facts}
    return {'type': 'metadata-patch', 'target': {'kind': 'entity', 'value': {'kind': kind, 'id': identity}},
            'source_id': 'native-test', 'set': {k: {'value': {'type': 'bool' if isinstance(v, bool) else 'text', 'value': v}}
                                              for k, v in facts.items()}, 'unset': []}


class NativeSidebarTests(unittest.TestCase):
    def setUp(self):
        config = (ROOT / 'data/sidebar/daily-driver.kdl').read_bytes()
        error = C.c_char_p()
        self.core = lib.andamento_create(config, len(config), C.byref(error))
        self.assertTrue(self.core, error.value)
        facts = [patch('project', 'p', **{'flotilla.project': 'p'}),
                 patch('checkout', 'checkout', **{'flotilla.project': 'p', 'action.primary.recipe': 'exec /bin/sh'}),
                 patch('convoy', 'c', **{'flotilla.project': 'p', 'flotilla.convoy': 'c',
                                      'action.primary.target': 'vessel:v', 'action.primary.recipe': 'flotilla attach --host test terminal'}),
                 patch('convoy', 'c2', **{'flotilla.project': 'p', 'flotilla.convoy': 'c2'}),
                 patch('vessel', 'v', **{'flotilla.project': 'p', 'flotilla.convoy': 'c', 'flotilla.vessel': 'v',
                                       'action.primary.recipe': 'flotilla attach --host test terminal', 'status.attention': True}),
                 patch('vessel', 'unavailable', **{'flotilla.convoy': 'c2'}),
                 patch('session', 'session', **{'action.primary.recipe': 'flotilla attach terminal'}),
                 patch('issue', 'issue', **{'flotilla.project': 'p'})]
        for item in facts:
            self.assertEqual(lib.andamento_apply_patch_json(self.core, 0, Text.of(json.dumps(item)), None), 1)
        self.snapshots = []

    def test_snapshot_validity_tracks_core_changes(self):
        displayed = lib.andamento_snapshot_acquire(self.core, None)
        self.snapshots.append(displayed)
        self.assertEqual(lib.andamento_snapshot_is_current(self.core, displayed, None), 1)
        self.assertEqual(lib.andamento_tick(self.core, 100, None), 1)
        self.assertEqual(lib.andamento_snapshot_is_current(self.core, displayed, None), 1)
        update = patch('vessel', 'v', **{'action.primary.recipe': 'exec changed'})
        self.assertEqual(lib.andamento_apply_patch_json(self.core, 100, Text.of(json.dumps(update)), None), 1)
        self.assertEqual(lib.andamento_snapshot_is_current(self.core, displayed, None), 0)
        fresh = lib.andamento_snapshot_acquire(self.core, None)
        self.snapshots.append(fresh)
        self.assertEqual(lib.andamento_apply_patch_json(self.core, 200, Text.of(json.dumps(update)), None), 1)
        self.assertEqual(lib.andamento_snapshot_is_current(self.core, fresh, None), 1)

    def test_finished_toggle_keeps_failures_and_active_entries(self):
        for phase in ('active', 'landed', 'cancelled', 'abandoned', 'failed', 'pending'):
            for item in [patch('convoy', phase, **{'flotilla.project': 'p', 'flotilla.convoy': phase,
                                                  'flotilla.convoy.phase': phase, 'status.attention': True}),
                         patch('vessel', phase + '-worker', **{'flotilla.convoy': phase,
                                   'flotilla.convoy.phase': phase, 'status.attention': True})]:
                self.assertEqual(lib.andamento_apply_patch_json(self.core, 1, Text.of(json.dumps(item)), None), 1)
        for show in (False, True, False):
            snapshot, nodes = self.snapshot()
            ids = {node.entity_id.string() for node in nodes}
            for phase in ('active', 'failed', 'pending'):
                self.assertIn(phase, ids)
                self.assertIn(phase + '-worker', ids)
            for phase in ('landed', 'cancelled', 'abandoned'):
                self.assertEqual(phase in ids, show)
                self.assertEqual(phase + '-worker' in ids, show)
            controls = []
            for node in nodes:
                for index in range(node.first_control, node.first_control + node.control_count):
                    control = Control()
                    self.assertTrue(lib.andamento_snapshot_control(snapshot, index, C.byref(control)))
                    if control.label.string() == 'Show finished':
                        controls.append(control)
            self.assertEqual(len(controls), 1)
            self.assertEqual(bool(controls[0].checked), show)
            self.assertTrue(lib.andamento_dispatch(self.core, snapshot, controls[0].action, None))

    def tearDown(self):
        for snapshot in self.snapshots:
            lib.andamento_snapshot_release(snapshot)
        lib.andamento_destroy(self.core)

    def snapshot(self):
        snapshot = lib.andamento_snapshot_acquire(self.core, None)
        self.assertTrue(snapshot)
        self.snapshots.append(snapshot)
        self.assertEqual(lib.andamento_snapshot_diagnostic_count(snapshot), 0)
        nodes = []
        for i in range(lib.andamento_snapshot_node_count(snapshot)):
            node = Node()
            self.assertEqual(lib.andamento_snapshot_node(snapshot, i, C.byref(node)), 1)
            nodes.append(node)
        return snapshot, nodes

    def dispatch(self, snapshot, action):
        self.assertEqual(lib.andamento_dispatch(self.core, snapshot, action, None), 1)
        batch = lib.andamento_effects_take(self.core, None)
        self.assertEqual(lib.andamento_effects_count(batch), 1)
        effect = Effect()
        lib.andamento_effects_get(batch, 0, C.byref(effect))
        result = (effect.kind, effect.request_id, effect.workspace_id, effect.recipe.string())
        lib.andamento_effects_release(batch)
        return result

    def test_hierarchy_and_truthful_openability(self):
        _, nodes = self.snapshot()
        find = lambda identity: next(n for n in nodes if n.entity_id.string() == identity)
        self.assertEqual(nodes[find('checkout').parent].entity_id.string(), 'p')
        self.assertEqual(nodes[find('c').parent].entity_id.string(), 'p')
        self.assertEqual(nodes[find('v').parent].entity_id.string(), 'c')
        self.assertTrue(find('checkout').openable)
        self.assertTrue(find('v').openable)  # one-vessel convoy must not hide its activation
        self.assertTrue(find('session').openable)
        self.assertFalse(find('unavailable').openable)
        self.assertFalse(find('issue').openable)

    def test_open_from_attention_then_focus_from_tree(self):
        snapshot, nodes = self.snapshot()
        vessels = [n for n in nodes if n.entity_id.string() == 'v']
        self.assertEqual(len(vessels), 2)
        kind, request, _, recipe = self.dispatch(snapshot, vessels[1].activate)
        self.assertEqual(kind, 1)  # materialize, never inspect
        self.assertIn('flotilla attach', recipe)
        self.assertEqual(lib.andamento_complete(self.core, request, 1, 42, Text.of(''), None), 1)
        workspace = Workspace(42, 0, Text.of('v'), 1)
        self.assertEqual(lib.andamento_observe(self.core, C.byref(workspace), 1, None, 0, None), 1)
        snapshot, nodes = self.snapshot()
        vessels = [n for n in nodes if n.entity_id.string() == 'v']
        self.assertTrue(all(n.state == 3 and n.workspace_id == 42 and n.selected for n in vessels))
        convoy = next(n for n in nodes if n.entity_id.string() == 'c')
        self.assertEqual((convoy.state, convoy.workspace_id, convoy.selected), (3, 42, 1))
        kind, _, workspace_id, _ = self.dispatch(snapshot, vessels[0].activate)
        self.assertEqual((kind, workspace_id), (0, 42))  # focus, no duplicate

    def test_nested_collapse_survives_refresh_and_parent_reopening(self):
        def toggle(identity):
            snapshot, nodes = self.snapshot()
            node = next(n for n in nodes if n.entity_id.string() == identity)
            self.assertNotEqual(node.toggle, Size(-1).value)
            self.assertEqual(lib.andamento_dispatch(self.core, snapshot, node.toggle, None), 1)

        toggle('c')
        toggle('p')
        _, before = self.snapshot()
        keys = [(n.key.string(), n.parent) for n in before]
        # A producer update and host observation must not reset collapse, change
        # placement identities, or remove the children needed for reopening.
        update = patch('vessel', 'v', **{'display.label': 'renamed vessel'})
        self.assertEqual(lib.andamento_apply_patch_json(self.core, 1, Text.of(json.dumps(update)), None), 1)
        self.assertEqual(lib.andamento_observe(self.core, None, 0, None, 0, None), 1)
        _, after = self.snapshot()
        self.assertEqual([(n.key.string(), n.parent) for n in after], keys)
        find = lambda identity: next(n for n in after if n.entity_id.string() == identity)
        self.assertTrue(find('p').collapsed)
        self.assertTrue(find('c').collapsed)
        self.assertFalse(find('c2').collapsed)
        project_index = next(i for i, n in enumerate(after) if n.entity_id.string() == 'p')
        self.assertEqual(after[project_index + 1].parent, project_index)
        toggle('p')
        _, after = self.snapshot()
        self.assertFalse(find('p').collapsed)
        self.assertTrue(find('c').collapsed)
        self.assertFalse(find('c2').collapsed)
        # The same vessel's Attention placement is still independently present.
        self.assertEqual(sum(n.entity_id.string() == 'v' for n in after), 2)
        toggle('c')
        _, after = self.snapshot()
        self.assertFalse(find('c').collapsed)

    def test_issues_toggle_filters_placements_and_restores_identity(self):
        snapshot, nodes = self.snapshot()
        original = next(n.key.string() for n in nodes if n.entity_id.string() == 'issue')
        for checked in (True, False):
            controls = []
            for node in nodes:
                for index in range(node.first_control, node.first_control + node.control_count):
                    control = Control()
                    self.assertTrue(lib.andamento_snapshot_control(snapshot, index, C.byref(control)))
                    if control.label.string() == 'Issues':
                        controls.append(control)
            self.assertEqual(len(controls), 1)
            self.assertEqual((controls[0].value_kind, bool(controls[0].checked)), (1, checked))
            self.assertTrue(lib.andamento_dispatch(self.core, snapshot, controls[0].action, None))
            snapshot, nodes = self.snapshot()
            self.assertEqual(any(n.entity_id.string() == 'issue' for n in nodes), not checked)
            self.assertTrue(any(n.entity_id.string() == 'v' for n in nodes))
        self.assertEqual(next(n.key.string() for n in nodes if n.entity_id.string() == 'issue'), original)

    def test_abbreviated_row_retains_full_label_and_declared_field_slots(self):
        config = (ROOT / 'data/sidebar/daily-driver.kdl').read_text().replace(
            'for "vessel" kind="vessel"', 'for "vessel" kind="vessel" tier="short"')
        self.assertEqual(lib.andamento_configure(self.core, Text.of(config), None), 1)
        update = patch('vessel', 'v', **{'display.label': 'Full worker label', 'display.label.short': 'w'})
        self.assertEqual(lib.andamento_apply_patch_json(self.core, 1, Text.of(json.dumps(update)), None), 1)
        snapshot, nodes = self.snapshot()
        vessel = next(n for n in nodes if n.entity_id.string() == 'v')
        fields = []
        for index in range(vessel.first_field, vessel.first_field + vessel.field_count):
            field = Field()
            self.assertEqual(lib.andamento_snapshot_field(snapshot, index, C.byref(field)), 1)
            fields.append(field.text.string())
        self.assertEqual(fields, ['w', 'vessel', ''])
        self.assertEqual(vessel.label.string(), 'Full worker label')
        self.assertTrue(vessel.openable)

    def test_loop_keys_group_siblings_but_separate_attention_aliases(self):
        update = patch('vessel', 'v2', **{'flotilla.project': 'p', 'flotilla.convoy': 'c'})
        self.assertEqual(lib.andamento_apply_patch_json(self.core, 1, Text.of(json.dumps(update)), None), 1)
        snapshot, nodes = self.snapshot()
        keys = {}
        for index, node in enumerate(nodes):
            if node.entity_id.string() not in ('v', 'v2'):
                continue
            key = Text()
            self.assertEqual(lib.andamento_snapshot_node_loop_key(snapshot, index, C.byref(key)), 1)
            self.assertTrue(key.len)
            keys.setdefault(node.entity_id.string(), []).append(key.string())
        self.assertEqual(keys['v'][0], keys['v2'][0])
        self.assertNotEqual(keys['v'][0], keys['v'][1])

    def test_checkout_opens_instead_of_inspecting(self):
        snapshot, nodes = self.snapshot()
        node = next(n for n in nodes if n.entity_id.string() == 'checkout')
        self.assertEqual(self.dispatch(snapshot, node.activate)[0], 1)


if __name__ == '__main__':
    lib = load(Path(sys.argv.pop(1)))
    unittest.main()
