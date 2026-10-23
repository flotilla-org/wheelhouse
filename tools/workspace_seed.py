"""A Dashboard seeded with one workspace of tabs, as the native drivers start.

A workspace's arrangement and Slots live in Andamento's workspace record, not
in the presentation file, which keeps only this device's part of them
(native/src/uishell/uishell_workspace_store.c). A driver that needs particular
Views open writes both: the window, naming the workspace, in the
presentation file, and the workspace's record in the Dashboard's
directory, whose core imports it on its first load. Records are
Andamento's own JSON (andamento-core, `WorkspaceRecord`).
"""
import json
from pathlib import Path
import uuid


def terminal(command, cwd=None):
    """A terminal Slot's View Spec: a shell command line, optionally in `cwd`."""
    return {'content': {'Local': {'Command': {'line': {'Shell': command}, 'cwd': cwd}}}, 'presentation': 'terminal'}


def jackstay(launcher, endpoint):
    """A Jackstay Slot's View Spec: its launcher setting and endpoint."""
    return {'content': {'Local': {'Jackstay': {'launcher': launcher, 'endpoint': endpoint}}}, 'presentation': 'jackstay'}


def seed(dashboard, presentation, specs, size=(900, 600), label=None, selected=0):
    """Write `dashboard`'s workspace record and the presentation file `presentation`
    for one window with a workspace of one panel holding `specs` as tabs,
    `selected` its Selected View. With `label`, the workspace is a child of
    the window; without, the window's own. Returns the Workspace ID."""
    dashboard, presentation = Path(dashboard), Path(presentation)
    workspace_id = str(uuid.uuid4())
    keys = ['u:%d' % (i+1) for i in range(len(specs))]
    record = {
        'subject': {'entity': None, 'retained': []},
        'slots': {'baseline': None, 'edits': {},
                  'user': [{'key': key, 'spec': spec, 'rebind': 'Replace'} for key, spec in zip(keys, specs)],
                  'name': label, 'mood': None, 'departed': {}},
        'arrangement': {'owned': True,
                        'doc': {'root': {'id': '1', 'weight': 1.0, 'node': {'Tabs': {'tabs': keys, 'selected': keys[selected]}}}},
                        'placed': [], 'soft': {}, 'provider_changed': False},
        'resolutions': {},
    }
    dashboard.mkdir(parents=True, exist_ok=True)
    (dashboard / ('workspace-%s.record' % workspace_id)).write_text(json.dumps(record))
    # The presentation file names the workspace; `arrangement_presentation`
    # marks one Andamento keeps the arrangement of.
    owner = 'workspace_id: "%s"\n  arrangement_presentation: { }\n' % workspace_id
    if label is not None:
        owner = 'workspace:\n  {\n  label: %s\n  %s  }\n' % (json.dumps(label), owner)
    presentation.parent.mkdir(parents=True, exist_ok=True)
    presentation.write_text('// uishell 0.1.0 presentation file\n\nwindow:\n{\n  size: %d %d\n  %s}\n' % (size[0], size[1], owner))
    return workspace_id
