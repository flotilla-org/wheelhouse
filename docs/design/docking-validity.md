# Declared docking validity

`src/shell/shell_docking.h` declares View registrations, their traits and
three host kinds. `rd_dock_check` returns a named rule: host acceptance,
Workspace Subject, minimum width, singleton, one Control Surface per
Controlled Split, exclusion of a selector from its selected Workspace Region,
or refusal to close the workspace-selecting View. It has no UI or gesture
dependency.

A proposal describes the resulting placement, including instance and Control
Surface counts. A move retains its instance; creation or closure changes the
count. The selected Workspace Region is an identity, not the Visible
Workspace, so switching workspaces cannot make an invalid placement valid.

The sidebar accepts section Views, a Workspace Region accepts content Views,
and a floating Panel accepts Views that do not need a host. `sessions` can
stack as a section; other current content Views remain content-only. Their
existing self-contained targets need no Workspace Subject. User content Views
impose no additional minimum width; the scroll-region diagnostic fixture
declares a 128px minimum to exercise production drag-query width checks. These
declarations do not move sidebar content.

The registration list includes shell-dispatched `pending`, `watch` and
`getting_started` Views as well as visualizer hooks. Adding a visualizer
through the shared list requires declaring traits; UI registration also
asserts that the declaration exists.

## Current root Controlled Split adapter

The Control Surface remains implicit and rendered directly by the sidebar.
`workspace_selector` declares its traits and default sidebar placement for
#163. An explicit saved selector describes this same surface; it does not
create another. Its binding selects the root Workspace Region, covering the
legacy window mount and every remembered Workspace in that window. Moves to
another Controlled Split are refused because they would remove one split's
only Control Surface and add a second to the other. Nested Controlled Splits
will supply their own binding identities in #164.

The configuration adapter recognizes `panels` and numeric split children,
`control_views` (sidebar declarations), and `floating_panels`. The latter two
are placement declarations, not newly rendered docking hosts. At frame
boundaries and immediately after loading config, configuration changes trigger
repair of saved window layouts; mount getters never mutate or release nodes.
Invalid placements return to their registered default: the sidebar or the
first Panel leaf of their Workspace (the first remembered Workspace when the
source has no Workspace). View identity and settings are retained. Duplicate
singleton declarations prefer an instance with a valid placement; tree order
breaks ties, including when all copies need fallback. Missing explicit
selectors use the existing implicit Control Surface.

Every View drop site uses the production query `rd_dock_drag_target`, which
calls the same checker. Split targets pass the proposed width rather than the
unsplit Panel width. Move, split, create, duplicate and close commands also
consult the model. Restore and command execution use unmeasured geometry;
resizing a window does not relocate saved Views just because they currently
have too little space. Unknown View types cannot be created, duplicated or
docked without declared traits, but saved unknown content can always be closed
for recovery. The registry covers all current visualizer hooks and
shell-dispatched types; Rejected commands explain the named rule to the user.
Drag feedback omits invalid targets without issuing a command. Expression
drops are a separate content interaction.

## Headless verification

Run `python tools/test-docking.py`. The native harness uses the real config
parser and production drag-target query. It enumerates every registered View
and host, checks binding identities and named structural rules, and loads a
hand-edited invalid layout to verify fallback, settings retention, duplicate
repair, split-leaf placement and idempotence. It covers missing subjects,
minimum-width boundaries, unknown Views, invalid hosts and empty layouts. No
display or runtime provider is required.

Existing CI native builds run the suite with their configured compiler. Normal
developer builds opt in with `WHEELHOUSE_DOCKING_TESTS=1`; the standalone
runner also honors `CC`. The existing Panel diagnostics exercise actual
command dispatch (including refused operations and error messages),
non-mutating mount reads, and repair-generation stability on Linux and macOS.
