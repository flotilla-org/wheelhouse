# Managed primary terminal content

Implemented first slice, 24 September 2026. This supplies the content-update
mechanism needed by standing project-role workspaces (Wheelhouse #48 and
Flotilla #1908). Flotilla now publishes the roles, and the daily-driver template
places them on project rows; see [Standing roles](#standing-roles).

Andamento resolves optional workspace.primary.state (ready/held) and
workspace.primary.target facts, action.primary.recipe and git.root (the
working directory materialization also uses) into one desired terminal
descriptor. Intent identity remains the workspace's
sidebar_entity_kind/id; the resolved target identifies its changing backing
instance. The primary resource_id remains stable as the user moves the view.

Wheelhouse reads actual saved descriptors and applies typed Andamento plans.
For an initialized in-process terminal it prepares another process, validates
the plan token, releases the old runtime and installs the candidate. The same
view, panel and workspace remain. For an uninitialized view it updates saved
configuration without launching a terminal. It clears obsolete session bindings
and updates cwd and managed_target. User-added views are untouched.

Held, unavailable and unchanged content do not replace terminals. A failed
replacement retains old content and reports an error; selecting the workspace
through the sidebar explicitly retries. Missing/duplicate primary slots or a
user-replaced view are not overwritten. Daemon-backed replacements are rejected
in this slice because that backend needs separate preparation/lifetime coverage.

A view-state teardown callback releases terminal attachments and render caches
on view-state retirement or replacement. Shared daemon providers remain open;
only the per-view session handle is detached. The explicit replacement path is
serialized with metadata publication on the UI thread. Reconciliation scans are
gated by metadata/configuration changes rather than performed every frame.

Success currently means process creation, not completion of SSH/authentication
inside flotilla attach. Remote readiness and transition/history presentation
remain future work. Producer transport loss suspends updates once relevant facts
expire; it does not destroy the existing content.

## Local build and validation

Build against an Andamento checkout at or after the pinned revision (the
sibling `../andamento` by default, or `WHEELHOUSE_ANDAMENTO_DIR`):

```sh
bash build.sh wheelhouse
mkdir -p /tmp/wheelhouse-managed-check
./build/wheelhouse --user:/tmp/wheelhouse-managed-check/user --project:/tmp/wheelhouse-managed-check/project --managed_content_diagnostics
```

The diagnostic uses the production C adapter and shared core with real terminals
printing A and B. It checks retained identities, panel position, user-added
content, removal of an obsolete session ID, repeated observation and held/resume.
It does not alter the daily driver's configuration or contact Flotilla.

The earlier standalone lifecycle and native prototypes have been absorbed into
shared-core regression coverage and this native diagnostic.

Validation on macOS: the core/FFI suites passed 175 tests, the core/FFI WASM
build passed, and native managed-content, panel, preview, scrolling and sidebar
diagnostics passed. Targeted Clippy completes with existing warnings in untouched
core code; a strict warnings-as-errors run is not clean on that baseline.
The native managed-content diagnostic is added to macOS CI. Linux's current
CI build omits Ghostty VT, so this real-terminal check is not enabled there.

CI pins Andamento `82b2f235f62a27521eece1f6bedba431feeeb6b3` (andamento#103 on
main), which provides the managed-content and related-detail interfaces.

## Standing roles

Flotilla #1908 publishes each declared standing role as a `role` entity; the
fact contract is in Andamento's `docs/sidebar-design/managed-primary-content.md`.
`data/sidebar/daily-driver.kdl` places roles as inline actions on their project
row, ordered by role name and selected by entity kind, never by name. A held
role stays visible but has no recipe to open.

The role action is the current attempt. Entities grouped under a known role's
attempt (its convoy and vessels) are hidden unless the **Role attempts** toggle
is on, so the tree does not offer a second workspace for the same terminal.
Superseded and finished attempts follow their existing rules. A task convoy
that shares a role name is unaffected. When a declaration is removed, its open
workspace moves to Other workspaces.

Opening a role records the managed target its recipe resolves
(`andamento_effects_primary_target`) on the new primary slot. Without it, the
first reconciliation would restart the attachment that was just created.

Validation: `tools/test-native-sidebar.py` covers inline placement, two roles on
one project, hidden attempts and the toggle, a task convoy sharing a role name,
held roles, the recorded target, and declaration removal. The managed-content
diagnostic opens a role through the production effect path and checks that its
first plan is current. A live check needs a Flotilla release containing #1908.

The same change folds in project repository membership (Flotilla #1897): the
project hover card lists each `project_repository` relation through a loop in
`project/detail`, which Andamento evaluates as related-entity detail rather than
tree rows. `tools/test-native-sidebar.py` covers ordering, subpaths, exclusion of
other projects' memberships, and that memberships are never placed.
