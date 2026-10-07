#!/usr/bin/env python3
"""Check the shipped native template against Andamento's real typed C ABI."""
import ctypes as C
import itertools
import json
import os
import shutil
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

class RegionHints(C.Structure):
    _fields_ = [("default_host", Text), ("has_order", U32), ("order", C.c_int64)]


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
        'snapshot_region_hints': (U32, [Ptr, Size, C.POINTER(RegionHints)]),
        'snapshot_field': (U32, [Ptr, Size, C.POINTER(Field)]),
        'snapshot_copy_url_action': (Size, [Ptr, Size]),
        'snapshot_node_loop_key': (U32, [Ptr, Size, C.POINTER(Text)]),
        'apply_patch_json': (U32, [Ptr, U64, Text, Ptr]),
        'snapshot_acquire': (Ptr, [Ptr, Ptr]), 'snapshot_release': (None, [Ptr]),
        'snapshot_node_count': (Size, [Ptr]), 'snapshot_node': (U32, [Ptr, Size, C.POINTER(Node)]),
        'snapshot_control': (U32, [Ptr, Size, C.POINTER(Control)]),
        'snapshot_diagnostic_count': (Size, [Ptr]),
        'dispatch': (U32, [Ptr, Ptr, Size, Ptr]), 'effects_take': (Ptr, [Ptr, Ptr]),
        'effects_count': (Size, [Ptr]), 'effects_get': (U32, [Ptr, Size, C.POINTER(Effect)]),
        'effects_primary_target': (U32, [Ptr, Size, C.POINTER(Text)]),
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
            'source_id': 'native-test', 'set': {k: {'value': {'type': 'bool' if isinstance(v, bool) else 'entity-refs' if isinstance(v, list) else 'text', 'value': v}}
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

    def test_region_hints_match_shipped_order(self):
        # Shipped placement hints preserve today's order across the native ABI.
        snapshot = lib.andamento_snapshot_acquire(self.core, None)
        self.snapshots.append(snapshot)
        hints_seen = []
        for index in range(lib.andamento_snapshot_node_count(snapshot)):
            node, hints = Node(), RegionHints()
            self.assertEqual(lib.andamento_snapshot_node(snapshot, index, C.byref(node)), 1)
            if node.is_section:
                self.assertEqual(lib.andamento_snapshot_region_hints(snapshot, index, C.byref(hints)), 1)
                hints_seen.append((node.key.string(), hints.default_host.string(), hints.has_order, hints.order))
            else:
                self.assertEqual(lib.andamento_snapshot_region_hints(snapshot, index, C.byref(hints)), 0)
        # The workspace fallback is always emitted, unhinted; Wheelhouse places it last.
        self.assertEqual(hints_seen, [('tree', 'sidebar', 1, 10), ('sessions', 'sidebar', 1, 20),
                                     ('attention', 'sidebar', 1, 30), ('git', 'sidebar', 1, 40),
                                     ('andamento.unplaced-workspaces', '', 0, 0)])

    def open_workspace(self, identity, workspace_id):
        snapshot, nodes = self.snapshot()
        node = next(n for n in nodes if n.entity_id.string() == identity)
        kind, request, _, _ = self.dispatch(snapshot, node.activate)
        self.assertEqual(kind, 1)
        self.assertTrue(lib.andamento_complete(self.core, request, 1, workspace_id, Text.of(''), None))
        workspace = Workspace(workspace_id, 0, Text.of(identity), 1)
        self.assertTrue(lib.andamento_observe(self.core, C.byref(workspace), 1, None, 0, None))

    def publish_fixture(self):
        for line in (ROOT / 'data/sidebar/fixture.jsonl').read_text().splitlines():
            item = json.loads(line)
            item['source_id'] = 'native-test'
            self.assertEqual(lib.andamento_apply_patch_json(self.core, 0, Text.of(json.dumps(item)), None), 1)

    def values(self, snapshot, node, detail=False):
        start, count = (node.first_detail, node.detail_count) if detail else (node.first_field, node.field_count)
        result = []
        for index in range(start, start + count):
            field = Field()
            self.assertTrue(lib.andamento_snapshot_field(snapshot, index, C.byref(field)))
            result.append(field.text.string())
        return result

    def children(self, nodes, identity):
        parent = next(i for i, n in enumerate(nodes) if n.entity_id.string() == identity)
        return [n for n in nodes if n.parent == parent]

    @unittest.skipUnless(shutil.which('cc') or (os.environ.get('CI') == 'true' and sys.platform != 'win32'),
                         'A C compiler is required for the standalone layout test')
    def test_chip_layout_width_ladder_and_attention(self):
        self.assertIsNotNone(shutil.which('cc'), 'C compiler required for Unix CI width coverage')
        # Exercise the production resolver with measured widths independent of
        # the native display. Status space is subtracted by the caller.
        import subprocess
        import tempfile
        source = r"""
#include <assert.h>
#include "uishell/uishell_sidebar_chips.h"
int main(void) {
  float names[] = {180, 130, 100};
  UIShell_ChipMeasure chips[] = {{50, 1, 0, 0}, {60, 1, 0, 0}, {35, 0, 0, 0}, {35, 0, 0, 0}};
  UIShell_ChipLayout r = uishell_chip_layout(400, names, 80, chips, 4, 30);
  assert(r.tier == 0 && r.folded == 0);
  r = uishell_chip_layout(320, names, 80, chips, 4, 30);
  assert(r.tier == 1 && r.folded == 0);
  r = uishell_chip_layout(285, names, 80, chips, 4, 30);
  assert(r.tier == 2 && r.folded == 0);
  r = uishell_chip_layout(260, names, 80, chips, 4, 30);
  assert(r.folded == 0 && r.name_width == 80);
  r = uishell_chip_layout(240, names, 80, chips, 4, 30);
  assert(r.folded == 2 && !chips[0].folded && !chips[1].folded);
  assert(chips[2].folded && chips[3].folded && r.chip_width == 140);
  // Activity/selection pins a workspace even when it is last in order.
  chips[3].attention = 1;
  r = uishell_chip_layout(240, names, 80, chips, 4, 30);
  assert(r.folded == 1 && chips[2].folded && !chips[3].folded);
  assert(r.content_width > r.chip_width);
  // Impossible widths retain attention in the scrollable viewport, with
  // nonnegative name/viewport widths and the same caller-owned status slot.
  r = uishell_chip_layout(50, names, 80, chips, 4, 30);
  assert(!chips[0].folded && !chips[1].folded && !chips[3].folded);
  assert(r.name_width == 0 && r.chip_width == 50);
  r = uishell_chip_layout(400, names, 80, chips, 4, 30);
  assert(r.folded == 0 && !chips[2].folded);
  // Owner policy: an unopened action folds before a later quiet issue.
  // Generate widths spanning no fold, one fold, both folds and clipping.
  UIShell_ChipMeasure mixed[] = {{60, 0, 0, 1}, {50, 0, 0, 0}};
  for(int width = 0; width <= 300; width++) {
    r = uishell_chip_layout(width, names, 80, mixed, 2, 30);
    assert(!mixed[1].folded || mixed[0].folded);
    assert(r.name_width >= 0 && r.chip_width >= 0);
  }
  r = uishell_chip_layout(160, names, 80, mixed, 2, 30);
  assert(r.folded == 1 && mixed[0].folded && !mixed[1].folded);
  // At tighter width both classes must fold, after unopened-only folding above.
  r = uishell_chip_layout(80, names, 80, mixed, 2, 30);
  assert(r.folded == 2 && mixed[0].folded && mixed[1].folded);
  mixed[0].attention = 1;
  r = uishell_chip_layout(160, names, 80, mixed, 2, 30);
  assert(!mixed[0].folded && mixed[1].folded);
  return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            source_file = Path(directory) / 'chips.c'
            source_file.write_text(source)
            executable = Path(directory) / 'chips'
            subprocess.run(['cc', '-std=c99', '-Wall', '-Werror', '-I', str(ROOT / 'src'),
                            str(source_file), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    # The issue's multi-repository fixture is a scenario through the real core:
    # numeric order, duplicate edges, shared generations, missing forge and label tiers.
    def test_subject_fixture_joins_order_and_label_tiers(self):
        self.publish_fixture()
        snapshot, nodes = self.snapshot()
        subjects = [n for n in self.children(nodes, 'build') if n.entity_kind.string() in ('change_request', 'issue')]
        self.assertEqual([n.entity_id.string() for n in subjects], ['pr-281', 'pr-1000', 'no-forge', 'issue-137'])
        self.assertTrue(all(n.layout.string() == 'inline' for n in subjects))
        self.assertEqual([self.values(snapshot, n)[0] for n in subjects], ['!281', 'c!1000', '!2508', '#137'])
        self.assertEqual([self.values(snapshot, n)[:3] for n in subjects], [
            ['!281', 'change_request', 'ready_to_merge'],
            ['c!1000', 'change_request', 'ci_failing'],
            ['!2508', 'change_request', 'awaiting_review_response'],
            ['#137', 'issue', 'open']])
        self.assertNotIn('superseded', [n.entity_id.string() for n in nodes])
        self.toggle_variable('Show finished')
        _, nodes = self.snapshot()
        self.assertEqual([n.entity_id.string() for n in self.children(nodes, 'superseded')], ['pr-281', 'pr-1000'])
        self.toggle_variable('Issues')
        _, nodes = self.snapshot()
        self.assertNotIn('issue-137', [n.entity_id.string() for n in nodes])
        config = (ROOT / 'data/sidebar/daily-driver.kdl').read_text().replace('tier="medium"', 'tier="short"')
        self.assertTrue(lib.andamento_configure(self.core, Text.of(config), None))
        snapshot, nodes = self.snapshot()
        pr = next(n for n in self.children(nodes, 'build') if n.entity_id.string() == 'pr-1000')
        self.assertEqual(self.values(snapshot, pr)[0], '!1000')
        self.assertEqual(pr.label.string(), 'c!1000')

    # Each published readiness is exercised across changes, with no status.attention.
    # Finished subjects obey the existing toggle; pending/draft/conflicting stay in the tree.
    def test_subject_readiness_attention_and_finished_lifecycle(self):
        self.publish_fixture()
        for readiness, state in [('ready_to_merge', 'open'), ('awaiting_review_response', 'open'),
                ('ci_failing', 'open'), ('conflicting', 'open'), ('draft', 'draft'),
                ('merged_not_landed', 'merged'), ('closed', 'closed')]:
            update = patch('change_request', 'pr-281', **{'flotilla.change_request.readiness': readiness,
                                                        'flotilla.change_request.state': state})
            # Patch identity/label are omitted, matching producer partial updates.
            for key in ('entity.kind', 'entity.id', 'display.label'):
                update['set'].pop(key)
            self.assertTrue(lib.andamento_apply_patch_json(self.core, 0, Text.of(json.dumps(update)), None))
            for show in (False, True):
                snapshot, nodes = self.snapshot()
                tree = self.children(nodes, 'build')
                ids = [n.entity_id.string() for n in tree]
                self.assertEqual('pr-281' in ids, show or state not in ('merged', 'closed'))
                attention = [n.entity_id.string() for n in nodes if n.parent != Size(-1).value and
                             nodes[n.parent].is_section and nodes[n.parent].label.string() == 'attention']
                self.assertEqual('pr-281' in attention, readiness in ('ready_to_merge', 'ci_failing'))
                self.toggle_variable('Show finished')
        update = patch('issue', 'issue-137', **{'flotilla.issue.state': 'closed'})
        update['set'].pop('display.label')
        self.assertTrue(lib.andamento_apply_patch_json(self.core, 0, Text.of(json.dumps(update)), None))
        _, nodes = self.snapshot()
        self.assertNotIn('issue-137', [n.entity_id.string() for n in nodes])
        self.toggle_variable('Show finished')
        _, nodes = self.snapshot()
        self.assertIn('issue-137', [n.entity_id.string() for n in nodes])
        self.toggle_variable('Issues')
        _, nodes = self.snapshot()
        self.assertNotIn('issue-137', [n.entity_id.string() for n in nodes])

    # Canonical actions use the fixture forge's deliberately nonstandard URL shapes.
    # Missing forge keeps the row and short reference, with no URL or workspace action.
    def test_subject_actions_and_observation_details(self):
        self.publish_fixture()
        snapshot, nodes = self.snapshot()
        for identity, url in [('pr-281', 'https://forge.example/org/wheelhouse/review/281'),
                              ('issue-137', 'https://forge.example/org/wheelhouse/ticket/137')]:
            index = next(i for i, n in enumerate(nodes) if n.entity_id.string() == identity)
            self.assertEqual(self.dispatch(snapshot, nodes[index].activate)[0::3], (3, url))
            copy = lib.andamento_snapshot_copy_url_action(snapshot, index)
            self.assertNotEqual(copy, Size(-1).value)
            self.assertEqual(self.dispatch(snapshot, copy)[0::3], (4, url))
        missing = next(i for i, n in enumerate(nodes) if n.entity_id.string() == 'no-forge')
        self.assertEqual(lib.andamento_snapshot_copy_url_action(snapshot, missing), Size(-1).value)
        self.assertFalse(nodes[missing].openable)
        self.assertEqual(self.values(snapshot, nodes[missing])[0], '!2508')
        pr = next(n for n in nodes if n.entity_id.string() == 'pr-281')
        details = self.values(snapshot, pr, detail=True)
        for expected in ['Title: Render subjects', 'State: open', 'Checks: pass', 'Review: approved',
                         'Mergeable: mergeable', 'Checks observed: 2026-10-03T09:19:00Z']:
            self.assertIn(expected, details)
        forge = patch('forge', 'fixture', **{'flotilla.forge.web_url': 'https://new.example'})
        self.assertTrue(lib.andamento_apply_patch_json(self.core, 0, Text.of(json.dumps(forge)), None))
        self.assertFalse(lib.andamento_dispatch(self.core, snapshot, pr.activate, None))
        fresh, nodes = self.snapshot()
        pr = next(n for n in nodes if n.entity_id.string() == 'pr-281')
        self.assertEqual(self.dispatch(fresh, pr.activate)[3], 'https://new.example/org/wheelhouse/review/281')

    # Producer templates may be malformed: only http(s) URL actions may reach a host.
    def test_subject_rejects_non_web_forge_templates(self):
        self.publish_fixture()
        for template in ('file:///tmp/program', 'javascript:alert(1)', '/tmp/program', 'custom:handler'):
            update = patch('forge', 'fixture', **{'flotilla.forge.change_request_url_template': template})
            self.assertTrue(lib.andamento_apply_patch_json(self.core, 0, Text.of(json.dumps(update)), None))
            snapshot, nodes = self.snapshot()
            index = next(i for i, n in enumerate(nodes) if n.entity_id.string() == 'pr-281')
            self.assertEqual(lib.andamento_snapshot_copy_url_action(snapshot, index), Size(-1).value)
            self.assertEqual(self.dispatch(snapshot, nodes[index].activate)[0], 2)  # inspect, no URL launch

    # Role attachment/status come from lifted facts. Forward edges supply current
    # detail and oldest-first attempts even when IDs sort in the opposite order.
    def test_role_fixture_current_detail_and_ordered_attempts(self):
        self.publish_fixture()
        snapshot, nodes = self.snapshot()
        role = next(n for n in nodes if n.entity_id.string() == 'p/governor')
        self.assertTrue(role.openable)
        self.assertEqual(role.layout.string(), 'inline')
        self.assertIn('running', self.values(snapshot, role))
        details = self.values(snapshot, role, detail=True)
        self.assertIn('Current attempt: governor', details)
        self.assertIn('Attempt phase: active', details)
        self.assertIn('Attachment: ready', details)
        self.assertEqual(self.children(nodes, 'p/governor'), [])
        self.toggle_variable('Role history')
        _, nodes = self.snapshot()
        self.assertEqual([n.entity_id.string() for n in self.children(nodes, 'p/governor')], ['z-attempt-1', 'abandoned-1', 'abandoned-2', 'a-attempt-2'])
        self.toggle_variable('Show finished')
        _, nodes = self.snapshot()
        # Attempts have one placement under the role, including when finished work is shown.
        for identity in ('z-attempt-1', 'abandoned-1', 'abandoned-2', 'a-attempt-2'):
            instances = [n for n in nodes if n.entity_id.string() == identity]
            self.assertEqual(len(instances), 1)
            self.assertEqual(nodes[instances[0].parent].entity_id.string(), 'p/governor')
        self.toggle_variable('Role history')
        _, nodes = self.snapshot()
        self.assertEqual(self.children(nodes, 'p/governor'), [])

    def reset_core(self):
        # A fresh core is the process boundary used by the host restart path.
        for snapshot in self.snapshots:
            lib.andamento_snapshot_release(snapshot)
        self.snapshots.clear()
        lib.andamento_destroy(self.core)
        config = (ROOT / 'data/sidebar/daily-driver.kdl').read_bytes()
        self.core = lib.andamento_create(config, len(config), None)
        self.assertTrue(self.core)
        # These governor scenarios need only a project, unlike the broader setUp fixture.
        self.apply_fact(patch('project', 'p', **{'flotilla.project': 'p'}))

    def apply_fact(self, item):
        self.assertEqual(lib.andamento_apply_patch_json(self.core, 1, Text.of(json.dumps(item)), None), 1)

    def assert_running_governor(self, workspace_id=None, current=None, expect_history=False):
        snapshot, nodes = self.snapshot()
        role = next(n for n in nodes if n.entity_kind.string() == 'role' and n.entity_id.string() == 'p/governor')
        # label/kind/status positions are the native renderer contract (node_status reads field 2).
        self.assertEqual(self.values(snapshot, role)[2], 'running')
        if workspace_id is not None:
            self.assertEqual((role.state, role.workspace_id, role.selected), (3, workspace_id, 1))
        if current is not None:
            self.assertEqual([v for v in self.values(snapshot, role, detail=True) if v.startswith('Attempt phase:')],
                             ['Attempt phase: active'])
            self.assertIn('Current attempt: governor', self.values(snapshot, role, detail=True))
            # History rows retain distinct resource IDs even with identical names.
            attempts = self.children(nodes, 'p/governor')
            expected = current if expect_history else []
            self.assertEqual([n.entity_id.string() for n in attempts], expected)
            self.assertEqual(len({n.key.string() for n in attempts}), len(expected))

    # Guard for the #213 scenario (no failing replay yet): same-address governors keep the
    # role running in every catalog order. Six permutations cover order collisions.
    def test_running_governor_with_abandoned_same_address_attempts(self):
        identities = ('old-1', 'old-2', 'live')
        for order in itertools.permutations(identities):
            with self.subTest(order=order):
                self.reset_core()
                self.publish_role('governor', attempt='live-v', **{'status.state': 'running',
                    'flotilla.role.current_attempt': [{'kind': 'convoy', 'id': 'live'}],
                    'flotilla.role.attempts': [{'kind': 'convoy', 'id': i} for i in identities]})
                for identity in order:
                    self.publish_attempt(identity, 'governor', **{'display.label': 'governor',
                        'flotilla.convoy': 'governor',
                        'flotilla.convoy.phase': 'active' if identity == 'live' else 'abandoned',
                        'status.state': 'running' if identity == 'live' else 'ended'})
                self.assert_running_governor(current=list(identities))
                self.open_workspace('p/governor', 44)
                self.toggle_variable('Role history')
                self.assert_running_governor(44, list(identities), expect_history=True)

    # A workspace opened during attempt A follows the role through A -> B -> C.
    # Check both update interleavings, history visibility, and the host's restart
    # seam: saved subject kind/id selects a latent row, whose effect rebinds the
    # existing workspace ID. This exercises the ABI, not native CFG serialization.
    def test_governor_readmit_lifecycle_and_workspace_restore(self):
        for history, abandon_first, role_first in itertools.product((False, True), repeat=3):
            with self.subTest(history=history, abandon_first=abandon_first, role_first=role_first):
                self.reset_core()
                attempts = []
                facts = {}

                # These helpers retain exact patches for restart/removal replay; the
                # class helpers publish directly and do not keep a replay catalog.
                def publish(identity, phase):
                    for kind, resource in [('convoy', identity), ('vessel', identity + '-v')]:
                        item = patch(kind, resource, **{'flotilla.project': 'p', 'flotilla.role': 'p/governor',
                            'flotilla.convoy': 'governor', 'display.label': 'governor',
                            'flotilla.convoy.phase': phase,
                            'status.state': 'running' if phase == 'active' else 'ended'})
                        facts[(kind, resource)] = item
                        self.apply_fact(item)

                def update_role():
                    item = patch('role', 'p/governor', **{'flotilla.project': 'p',
                        'flotilla.role': 'p/governor', 'flotilla.role.name': 'governor',
                        'display.label': 'governor', 'status.state': 'running',
                        'action.primary.target': 'role:p/governor', 'action.primary.recipe': 'exec sh',
                        'workspace.primary.state': 'ready', 'workspace.primary.target': 'vessel:' + attempts[-1] + '-v',
                        'flotilla.role.current_attempt': [{'kind': 'convoy', 'id': attempts[-1]}],
                        'flotilla.role.attempts': [{'kind': 'convoy', 'id': i} for i in attempts]})
                    facts[('role', 'p/governor')] = item
                    self.apply_fact(item)

                attempts.append('A')
                publish('A', 'active')
                update_role()
                if history:
                    self.toggle_variable('Role history')
                self.open_workspace('p/governor', 44)
                self.assert_running_governor(44, attempts, expect_history=history)
                for identity in ('B', 'C'):
                    previous = attempts[-1]
                    if abandon_first:
                        publish(previous, 'abandoned')
                        self.assert_running_governor(44)
                    attempts.append(identity)
                    publish(identity, 'active')
                    update_role()
                    if not abandon_first:
                        publish(previous, 'abandoned')
                    self.assert_running_governor(44, attempts, expect_history=history)

                # Save only the subject identity the host stores, then start a fresh
                # core and replay the current producer facts before restoring it.
                saved = {'kind': 'role', 'id': 'p/governor', 'workspace_id': 44}
                self.reset_core()
                replay = list(facts.values())
                if role_first:
                    replay.sort(key=lambda item: item['target']['value']['kind'] != 'role')
                for item in replay:
                    self.apply_fact(item)
                if history:
                    self.toggle_variable('Role history')
                snapshot, nodes = self.snapshot()
                node = next(n for n in nodes if (n.entity_kind.string(), n.entity_id.string()) ==
                            (saved['kind'], saved['id']))
                self.assertTrue(node.openable)
                self.assertEqual(node.state, 1)  # latent, before host restore
                kind, request, _, _ = self.dispatch(snapshot, node.activate)
                self.assertEqual(kind, 1)
                self.assertTrue(lib.andamento_complete(self.core, request, 1, saved['workspace_id'], Text.of(''), None))
                workspace = Workspace(saved['workspace_id'], 0, Text.of('governor'), 1)
                self.assertTrue(lib.andamento_observe(self.core, C.byref(workspace), 1, None, 0, None))
                self.assert_running_governor(44, attempts, expect_history=history)

                # Deleting terminal catalog records must leave the same live role
                # and workspace; check each removal, as in the operator's cure.
                abandoned_resources = {resource for identity in attempts[:-1]
                                       for resource in (identity, identity + '-v')}
                for (_, resource), item in facts.items():
                    if resource not in abandoned_resources:
                        continue
                    self.apply_fact({'target': item['target'], 'source_id': item['source_id'],
                                     'set': {}, 'unset': list(item['set'])})
                    self.assert_running_governor(44)
                _, nodes = self.snapshot()
                self.assertFalse(any(n.entity_id.string() in abandoned_resources for n in nodes))

    def test_git_fixture_groups_and_materializes_worktrees(self):
        for line in (ROOT / 'data/sidebar/git-fixture.jsonl').read_text().splitlines():
            self.assertEqual(lib.andamento_apply_patch_json(self.core, 0, Text.of(line), None), 1)
        snapshot, nodes = self.snapshot()
        repos = [node for node in nodes if node.entity_kind.string() == 'repo']
        worktrees = [node for node in nodes if node.entity_kind.string() == 'worktree']
        self.assertEqual(len(repos), 2)
        self.assertEqual(len(worktrees), 4)
        for node in worktrees:
            parent = nodes[node.parent]
            self.assertEqual(parent.entity_kind.string(), 'repo')
            self.assertIn(parent.entity_id.string().split('/')[-1], node.entity_id.string())
            self.assertEqual(node.state, 1)  # latent
            fields = []
            for index in range(node.first_field, node.first_field + node.field_count):
                field = Field()
                self.assertEqual(lib.andamento_snapshot_field(snapshot, index, C.byref(field)), 1)
                fields.append(field.text.string())
            self.assertTrue(any(text.startswith('dirty:') for text in fields), fields)
        self.assertEqual(lib.andamento_dispatch(self.core, snapshot, worktrees[0].activate, None), 1)
        batch = lib.andamento_effects_take(self.core, None)
        effect = Effect()
        self.assertEqual(lib.andamento_effects_get(batch, 0, C.byref(effect)), 1)
        self.assertEqual(effect.recipe.string(), '/bin/sh')
        self.assertEqual(effect.has_cwd, 1)
        self.assertEqual(effect.cwd.string(), worktrees[0].entity_id.string())
        lib.andamento_effects_release(batch)

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

    def test_superseded_failure_is_hidden_but_current_failure_remains(self):
        for identity, superseded in [('old-governor', True), ('current-governor', False)]:
            for kind, name in [('convoy', identity), ('vessel', identity + '-worker')]:
                item = patch(kind, name, **{'flotilla.project': 'p', 'flotilla.convoy': identity,
                             'flotilla.convoy.phase': 'failed', 'flotilla.convoy.superseded': superseded,
                             'status.state': 'failed', 'status.attention': True})
                self.assertEqual(lib.andamento_apply_patch_json(self.core, 1, Text.of(json.dumps(item)), None), 1)
        snapshot, nodes = self.snapshot()
        ids = {node.entity_id.string() for node in nodes}
        self.assertNotIn('old-governor', ids)
        self.assertNotIn('old-governor-worker', ids)
        self.assertIn('current-governor', ids)
        self.assertIn('current-governor-worker', ids)
        for node in nodes:
            for index in range(node.first_control, node.first_control + node.control_count):
                control = Control()
                self.assertTrue(lib.andamento_snapshot_control(snapshot, index, C.byref(control)))
                if control.label.string() == 'Show finished':
                    self.assertTrue(lib.andamento_dispatch(self.core, snapshot, control.action, None))
        _, shown = self.snapshot()
        shown_ids = {node.entity_id.string() for node in shown}
        for identity in ('old-governor', 'old-governor-worker', 'current-governor', 'current-governor-worker'):
            self.assertIn(identity, shown_ids)

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

    # Facts shaped as flotilla pm connect publishes standing roles (flotilla#1908).
    def publish_role(self, name, ready=True, attempt=None, **extra):
        identity = 'p/' + name
        facts = {'flotilla.project': 'p', 'flotilla.role': identity, 'flotilla.role.name': name,
                 'display.label': name, 'action.primary.target': 'role:' + identity,
                 'workspace.primary.state': 'ready' if ready else 'held', **extra}
        if ready:
            facts['workspace.primary.target'] = 'vessel:' + attempt
            facts['action.primary.recipe'] = 'flotilla attach --host test ' + attempt
        item = patch('role', identity, **facts)
        if not ready:
            item['unset'] = ['workspace.primary.target', 'action.primary.recipe']
        self.assertEqual(lib.andamento_apply_patch_json(self.core, 1, Text.of(json.dumps(item)), None), 1)

    def publish_attempt(self, convoy, role, **facts):
        for kind, identity in [('convoy', convoy), ('vessel', convoy + '-v')]:
            item = patch(kind, identity, **{'flotilla.project': 'p', 'flotilla.convoy': convoy,
                         'flotilla.convoy.standing': True, **({'flotilla.role': 'p/' + role} if role else {}), **facts})
            self.assertEqual(lib.andamento_apply_patch_json(self.core, 1, Text.of(json.dumps(item)), None), 1)

    def toggle_variable(self, label):
        snapshot, nodes = self.snapshot()
        for node in nodes:
            for index in range(node.first_control, node.first_control + node.control_count):
                control = Control()
                self.assertTrue(lib.andamento_snapshot_control(snapshot, index, C.byref(control)))
                if control.label.string() == label:
                    self.assertTrue(lib.andamento_dispatch(self.core, snapshot, control.action, None))
                    return
        self.fail('no control ' + label)

    def test_standing_roles_are_project_rows_and_replace_their_attempts(self):
        self.publish_role('quartermaster', attempt='q-v', **{'flotilla.role.attempts': [{'kind': 'convoy', 'id': 'q'}]})
        self.publish_role('governor', attempt='g-v', **{'flotilla.role.attempts': [{'kind': 'convoy', 'id': 'g'}]})
        self.publish_attempt('g', 'governor', **{'status.attention': True})
        self.publish_attempt('q', 'quartermaster')
        # A task convoy that happens to share a role name is not a role attempt.
        self.publish_attempt('task', None, **{'display.label': 'governor', 'flotilla.convoy.standing': False})
        snapshot, nodes = self.snapshot()
        project = next(i for i, n in enumerate(nodes) if n.entity_id.string() == 'p' and not n.is_section)
        roles = [n for n in nodes if n.entity_kind.string() == 'role']
        self.assertEqual([n.entity_id.string() for n in roles], ['p/governor', 'p/quartermaster'])
        self.assertTrue(all(n.parent == project and n.openable for n in roles))
        self.assertTrue(all(n.layout.string() == 'inline' for n in roles),
                        'standing roles must be actions on the project row')
        ids = {n.entity_id.string() for n in nodes}
        for hidden in ('g', 'g-v', 'q', 'q-v'):
            self.assertNotIn(hidden, ids)
        self.assertIn('task', ids)
        self.toggle_variable('Role history')
        _, nodes = self.snapshot()
        ids = {n.entity_id.string() for n in nodes}
        for shown in ('g', 'g-v', 'q', 'q-v', 'task', 'p/governor'):
            self.assertIn(shown, ids)

    def test_opening_a_role_records_its_current_managed_target(self):
        self.publish_role('governor', attempt='g-v')
        snapshot, nodes = self.snapshot()
        role = next(n for n in nodes if n.entity_id.string() == 'p/governor')
        self.assertEqual(lib.andamento_dispatch(self.core, snapshot, role.activate, None), 1)
        batch = lib.andamento_effects_take(self.core, None)
        effect, target = Effect(), Text()
        self.assertEqual(lib.andamento_effects_get(batch, 0, C.byref(effect)), 1)
        self.assertEqual((effect.kind, effect.recipe.string()), (1, 'flotilla attach --host test g-v'))
        self.assertEqual(lib.andamento_effects_primary_target(batch, 0, C.byref(target)), 1)
        self.assertEqual(target.string(), 'vessel:g-v')
        lib.andamento_effects_release(batch)

    def test_held_and_removed_roles(self):
        self.publish_role('governor', ready=False)
        _, nodes = self.snapshot()
        held = next(n for n in nodes if n.entity_id.string() == 'p/governor')
        self.assertFalse(held.openable)  # visible and inspectable, never a stale attachment
        self.publish_role('governor', attempt='g-v')
        self.open_workspace('p/governor', 44)
        _, nodes = self.snapshot()
        role = next(n for n in nodes if n.entity_id.string() == 'p/governor')
        self.assertEqual((role.workspace_id, role.selected), (44, 1))
        # A removed declaration retracts the action; its open workspace survives.
        removed = patch('role', 'p/governor')
        removed['set'] = {}
        removed['unset'] = ['entity.kind', 'entity.id', 'display.label', 'flotilla.project', 'flotilla.role',
                            'flotilla.role.name', 'action.primary.target', 'action.primary.recipe',
                            'workspace.primary.state', 'workspace.primary.target']
        self.assertEqual(lib.andamento_apply_patch_json(self.core, 1, Text.of(json.dumps(removed)), None), 1)
        _, nodes = self.snapshot()
        # An open removed role remains visible with its ended marker.
        self.assertTrue(any(n.entity_id.string() == 'p/governor' and n.workspace_id == 44 for n in nodes))
        self.toggle_variable('Show finished')
        snapshot, nodes = self.snapshot()
        role = next(n for n in nodes if n.entity_id.string() == 'p/governor')
        self.assertEqual(role.workspace_id, 44)
        self.assertEqual(self.values(snapshot, role)[2], 'ended')

    def test_project_hover_lists_repository_membership_without_placing_it(self):
        # Facts shaped as flotilla pm connect publishes membership (flotilla#1897).
        count = patch('project', 'p', **{'flotilla.project': 'p'})
        count['set']['flotilla.project.repository_count'] = {'value': {'type': 'integer', 'value': 2}}
        items = [count,
                 patch('project_repository', 'm-web', **{'flotilla.project': 'p', 'display.label': 'org/web',
                       'flotilla.membership.repository_key': 'repo-web'}),
                 patch('project_repository', 'm-docs', **{'flotilla.project': 'p', 'display.label': 'org/docs',
                       'flotilla.membership.repository_key': 'repo-docs', 'flotilla.membership.subpath': 'guide'}),
                 patch('project', 'q', **{'flotilla.project': 'q'}),
                 patch('project_repository', 'm-other', **{'flotilla.project': 'q', 'display.label': 'org/other'})]
        for item in items:
            self.assertEqual(lib.andamento_apply_patch_json(self.core, 1, Text.of(json.dumps(item)), None), 1)
        snapshot, nodes = self.snapshot()
        project = next(n for n in nodes if n.entity_id.string() == 'p' and not n.is_section)
        detail = []
        for index in range(project.first_detail, project.first_detail + project.detail_count):
            field = Field()
            self.assertEqual(lib.andamento_snapshot_field(snapshot, index, C.byref(field)), 1)
            detail.append(field.text.string())
        start = detail.index('Repositories: 2')
        self.assertEqual(detail[start:], ['Repositories: 2', '  org/docs', '    path: guide', '  org/web'])
        self.assertFalse(any(n.entity_kind.string() == 'project_repository' for n in nodes))

    # Lifecycle scenario through the real daily-driver KDL and C ABI: an ended
    # workspace stays on its subject, follows Show finished, and remains focusable.
    def test_ended_workspace_retains_subject_after_producer_removal(self):
        self.open_workspace('v', 42)
        # Removal has the real producer shape: identity unsets and a source heartbeat.
        removal = patch('vessel', 'v', source='flotilla')
        removal['set'] = {'source': removal['set']['source']}
        removal['unset'] = ['entity.kind', 'entity.id', 'display.label', 'flotilla.project',
                            'flotilla.convoy', 'flotilla.vessel', 'action.primary.recipe', 'status.attention']
        self.assertTrue(lib.andamento_apply_patch_json(self.core, 1, Text.of(json.dumps(removal)), None))
        _, nodes = self.snapshot()
        self.assertTrue(any(n.workspace_id == 42 for n in nodes))
        self.toggle_variable('Show finished')
        snapshot, nodes = self.snapshot()
        retained = next(n for n in nodes if n.workspace_id == 42 and n.entity_id.string() == 'v')
        self.assertEqual((retained.entity_kind.string(), retained.entity_id.string(), retained.state), ('vessel', 'v', 3))
        self.assertEqual(self.values(snapshot, retained)[2], 'ended')
        parent = nodes[retained.parent]
        self.assertEqual((parent.entity_kind.string(), parent.entity_id.string()), ('convoy', 'c'))
        self.assertFalse(any(n.entity_kind.string() == 'andamento.workspace' for n in nodes))
        kind, request, identity, _ = self.dispatch(snapshot, retained.activate)
        self.assertEqual((kind, identity), (0, 42))
        self.assertTrue(lib.andamento_complete(self.core, request, 0, 0, Text.of(''), None))
        self.assertTrue(lib.andamento_observe(self.core, None, 0, None, 0, None))
        _, nodes = self.snapshot()
        self.assertFalse(any(n.workspace_id == 42 for n in nodes))

    def test_open_ended_subject_is_visible_with_show_finished_off(self):
        # Owner ruling on #188: the open ended subject remains reachable in its
        # original project path, marked ended, without enabling Show finished.
        self.open_workspace('v', 42)
        update = patch('vessel', 'v', **{'flotilla.convoy.phase': 'landed'})
        self.assertTrue(lib.andamento_apply_patch_json(self.core, 1, Text.of(json.dumps(update)), None))
        snapshot, nodes = self.snapshot()
        live = [n for n in nodes if n.workspace_id == 42]
        self.assertTrue(live, 'open ended workspace has no sidebar entry')
        self.assertTrue(any(n.entity_kind.string() == 'vessel' for n in live))
        self.assertEqual(self.values(snapshot, next(n for n in live if n.entity_kind.string() == 'vessel'))[2], 'ended')
        self.assertTrue(lib.andamento_observe(self.core, None, 0, None, 0, None))
        _, nodes = self.snapshot()
        self.assertFalse(any(n.entity_id.string() == 'v' for n in nodes))

    def test_local_sidebar_has_only_observed_workspaces(self):
        lib.andamento_destroy(self.core)
        config = (ROOT / 'data/sidebar/local.kdl').read_bytes()
        self.core = lib.andamento_create(config, len(config), None)
        self.assertTrue(self.core)
        _, nodes = self.snapshot()
        self.assertFalse(any(not n.is_section or n.control_count for n in nodes))
        workspaces = (Workspace * 2)(Workspace(70, 0, Text.of('local'), 0),
                                     Workspace(71, 1, Text.of('local'), 1))
        self.assertEqual(lib.andamento_observe(self.core, workspaces, 2, None, 0, None), 1)
        snapshot, nodes = self.snapshot()
        entries = [n for n in nodes if not n.is_section]
        self.assertEqual([n.workspace_id for n in entries], [70, 71])
        self.assertTrue(all(n.entity_kind.string() == 'andamento.workspace' for n in entries))
        self.assertTrue(all(n.control_count == 0 for n in nodes))
        kind, request, identity, _ = self.dispatch(snapshot, entries[1].activate)
        self.assertEqual((kind, identity), (0, 71))
        self.assertEqual(lib.andamento_complete(self.core, request, 0, 0, Text.of(''), None), 1)
        workspaces[0].name = Text.of('Renamed')
        self.assertEqual(lib.andamento_observe(self.core, workspaces, 1, None, 0, None), 1)
        _, nodes = self.snapshot()
        self.assertEqual([(n.workspace_id, n.label.string()) for n in nodes if not n.is_section], [(70, 'Renamed')])

    def test_coverage_after_expiry_restart_and_unplaced_kind(self):
        # Unobserved subjects and unplaced kinds retain exact inventory IDs in
        # Other workspaces, including after a fresh core simulates daemon restart.
        for scenario in ('expired', 'restart', 'unplaced-kind'):
            with self.subTest(scenario=scenario):
                if scenario == 'restart':
                    lib.andamento_destroy(self.core)
                    config = (ROOT / 'data/sidebar/daily-driver.kdl').read_bytes()
                    self.core = lib.andamento_create(config, len(config), None)
                kind = 'unplaced' if scenario == 'unplaced-kind' else 'vessel'
                identity = 'missing'
                update = patch(kind, identity, **{'flotilla.project': 'p', 'action.primary.recipe': 'exec sh'})
                if scenario == 'expired':
                    for entry in update['set'].values():
                        entry['ttl_ms'] = 1
                    self.assertTrue(lib.andamento_apply_patch_json(self.core, 10, Text.of(json.dumps(update)), None))
                    self.assertTrue(lib.andamento_tick(self.core, 12, None))
                elif scenario == 'unplaced-kind':
                    self.assertTrue(lib.andamento_apply_patch_json(self.core, 12, Text.of(json.dumps(update)), None))
                binding = patch(kind, identity)
                binding['target'] = {'kind': 'tab', 'value': 70}
                self.assertTrue(lib.andamento_apply_patch_json(self.core, 12, Text.of(json.dumps(binding)), None))
                workspace = Workspace(70, 0, Text.of('Open workspace'), 1)
                self.assertTrue(lib.andamento_observe(self.core, C.byref(workspace), 1, None, 0, None))
                snapshot, nodes = self.snapshot()
                entries = [n for n in nodes if n.workspace_id == 70]
                self.assertEqual(len(entries), 1)
                self.assertEqual((entries[0].entity_kind.string(), entries[0].selected), ('andamento.workspace', 1))
                self.assertEqual(self.values(snapshot, nodes[entries[0].parent]), ['Other workspaces'])
                kind, request, identity, _ = self.dispatch(snapshot, entries[0].activate)
                self.assertEqual((kind, identity), (0, 70))
                self.assertTrue(lib.andamento_complete(self.core, request, 0, 0, Text.of(''), None))
                self.assertTrue(lib.andamento_observe(self.core, None, 0, None, 0, None))

    def test_unplaced_workspaces_focus_exact_ids_and_reject_stale_actions(self):
        workspaces = (Workspace * 2)(Workspace(70, 0, Text.of('local'), 0),
                                     Workspace(71, 1, Text.of('local'), 1))
        self.assertEqual(lib.andamento_observe(self.core, workspaces, 2, None, 0, None), 1)
        snapshot, nodes = self.snapshot()
        fallback = [n for n in nodes if n.entity_kind.string() == 'andamento.workspace']
        self.assertEqual([n.workspace_id for n in fallback], [70, 71])
        self.assertEqual([n.selected for n in fallback], [0, 1])
        self.assertNotEqual(fallback[0].key.string(), fallback[1].key.string())
        kind, request, identity, _ = self.dispatch(snapshot, fallback[1].activate)
        self.assertEqual((kind, identity), (0, 71))
        self.assertEqual(lib.andamento_complete(self.core, request, 0, 0, Text.of(''), None), 1)
        self.assertEqual(lib.andamento_observe(self.core, workspaces, 1, None, 0, None), 1)
        self.assertEqual(lib.andamento_dispatch(self.core, snapshot, fallback[1].activate, None), 0)
        _, nodes = self.snapshot()
        self.assertEqual([n.workspace_id for n in nodes if n.entity_kind.string() == 'andamento.workspace'], [70])

    # Terminal subject phases retain the workspace in place. A later stale
    # active publication cannot undo an authoritative end for the same identity.
    def test_finished_open_workspace_stays_visible_with_show_finished_off(self):
        self.open_workspace('v', 42)
        for phase in ('landed', 'active'):
            for kind, identity in [('convoy', 'c'), ('vessel', 'v')]:
                update = patch(kind, identity, **{'flotilla.convoy.phase': phase})
                self.assertTrue(lib.andamento_apply_patch_json(self.core, 1, Text.of(json.dumps(update)), None))
            _, nodes = self.snapshot()
            self.assertTrue(any(n.workspace_id == 42 for n in nodes))
        self.toggle_variable('Show finished')
        snapshot, nodes = self.snapshot()
        live = next(n for n in nodes if n.entity_id.string() == 'v' and n.workspace_id == 42)
        self.assertEqual(self.values(snapshot, live)[2], 'ended')
        kind, request, identity, _ = self.dispatch(snapshot, live.activate)
        self.assertEqual((kind, identity), (0, 42))
        self.assertTrue(lib.andamento_complete(self.core, request, 0, 0, Text.of(''), None))

    def test_hover_details_use_templates_without_changing_compact_rows(self):
        update = patch('vessel', 'v', **{'flotilla.vessel.host': 'remote',
                       'vcs.repo': 'github.com/example/repo', 'checkout.path': '/work/repo'})
        self.assertEqual(lib.andamento_apply_patch_json(self.core, 1, Text.of(json.dumps(update)), None), 1)
        update = patch('project', 'p')
        update['set']['count.convoys'] = {'value': {'type': 'integer', 'value': 3}}
        self.assertEqual(lib.andamento_apply_patch_json(self.core, 1, Text.of(json.dumps(update)), None), 1)
        snapshot, nodes = self.snapshot()
        def fields(start, count):
            values = []
            for index in range(start, start + count):
                field = Field()
                self.assertEqual(lib.andamento_snapshot_field(snapshot, index, C.byref(field)), 1)
                values.append(field.text.string())
            return values
        for vessel in [n for n in nodes if n.entity_id.string() == 'v']:
            detail = fields(vessel.first_detail, vessel.detail_count)
            self.assertIn('Host: remote', detail)
            self.assertIn('Repository: github.com/example/repo', detail)
            self.assertIn('Path: /work/repo', detail)
            self.assertNotIn('Host: remote', fields(vessel.first_field, vessel.field_count))
        project = next(n for n in nodes if n.entity_id.string() == 'p')
        self.assertIn('Convoys: 3', fields(project.first_detail, project.detail_count))

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

    def test_inline_siblings_keep_independent_bindings_when_parent_collapses(self):
        update = patch('vessel', 'review', **{'flotilla.convoy': 'c',
                                             'action.primary.recipe': 'exec review'})
        self.assertEqual(lib.andamento_apply_patch_json(self.core, 1, Text.of(json.dumps(update)), None), 1)
        snapshot, nodes = self.snapshot()
        convoy = next(n for n in nodes if n.entity_id.string() == 'c')
        children = [n for n in nodes if n.parent != Size(-1).value and
                    nodes[n.parent].entity_id.string() == 'c']
        self.assertEqual({n.entity_id.string() for n in children}, {'v', 'review'})
        self.assertTrue(all(n.layout.string() == 'inline' for n in children))
        original_keys = {n.entity_id.string(): n.key.string() for n in children}
        self.assertEqual(lib.andamento_dispatch(self.core, snapshot, convoy.toggle, None), 1)
        snapshot, nodes = self.snapshot()
        review = next(n for n in nodes if n.entity_id.string() == 'review')
        kind, request, _, recipe = self.dispatch(snapshot, review.activate)
        self.assertEqual((kind, recipe), (1, 'exec review'))
        self.assertEqual(lib.andamento_complete(self.core, request, 1, 43, Text.of(''), None), 1)
        workspace = Workspace(43, 0, Text.of('review'), 1)
        self.assertEqual(lib.andamento_observe(self.core, C.byref(workspace), 1, None, 0, None), 1)
        snapshot, nodes = self.snapshot()
        review = next(n for n in nodes if n.entity_id.string() == 'review')
        worker = next(n for n in nodes if n.entity_id.string() == 'v')
        self.assertEqual((review.workspace_id, review.selected), (43, 1))
        self.assertFalse(worker.selected)
        self.assertEqual(review.key.string(), original_keys['review'])
        self.assertEqual(worker.key.string(), original_keys['v'])
        self.assertTrue(next(n for n in nodes if n.entity_id.string() == 'c').collapsed)
        kind, _, identity, _ = self.dispatch(snapshot, review.activate)
        self.assertEqual((kind, identity), (0, 43))

    def test_subject_selection_with_convoy_and_vessel_workspaces(self):
        # A convoy and its vessel can both own open workspaces even when the
        # convoy action subsequently aliases the vessel. Only the subject owns selection.
        update = patch('convoy', 'c', **{'action.primary.target': 'convoy:c'})
        self.assertTrue(lib.andamento_apply_patch_json(self.core, 1, Text.of(json.dumps(update)), None))
        self.open_workspace('c', 43)
        self.open_workspace('v', 42)
        update = patch('convoy', 'c', **{'action.primary.target': 'vessel:v'})
        self.assertTrue(lib.andamento_apply_patch_json(self.core, 2, Text.of(json.dumps(update)), None))
        for selected in (42, 43):
            for project_closed in (False, True):
                for convoy_closed in (False, True):
                    with self.subTest(selected=selected, project=project_closed, convoy=convoy_closed):
                        for identity, closed in [('p', project_closed), ('c', convoy_closed)]:
                            snap, nodes = self.snapshot()
                            node = next(n for n in nodes if n.entity_id.string() == identity)
                            if bool(node.collapsed) != closed:
                                self.assertTrue(lib.andamento_dispatch(self.core, snap, node.toggle, None))
                        workspaces = (Workspace * 2)(Workspace(42, 0, Text.of('v'), selected == 42),
                                                     Workspace(43, 1, Text.of('c'), selected == 43))
                        self.assertTrue(lib.andamento_observe(self.core, workspaces, 2, None, 0, None))
                        _, nodes = self.snapshot()
                        convoy = next(n for n in nodes if n.entity_id.string() == 'c')
                        vessels = [n for n in nodes if n.entity_id.string() == 'v']
                        self.assertEqual((convoy.workspace_id, bool(convoy.selected)), (43, selected == 43))
                        self.assertTrue(all(n.workspace_id == 42 and bool(n.selected) == (selected == 42) for n in vessels))

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
        self.assertEqual((convoy.state, convoy.workspace_id, convoy.selected), (3, 42, 0))
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
        self.assertEqual(fields[:3], ['w', 'vessel', ''])
        self.assertIn('chip-short:w', fields)
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
