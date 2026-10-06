# Declared docking validity

`src/shell/shell_docking.h` declares View registrations, their traits and
three host kinds. `rd_dock_check` returns a named rule: host acceptance,
Workspace Subject, minimum width, singleton, one Control Surface per
Controlled Split, exclusion of a selector from its selected Workspace Region,
refusal to close the workspace-selecting View, or a different owning Controlled
Split level. It has no UI or gesture
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

Host style and owning level are separate checks. A View's level must match the
destination layout's level. The current adapter identifies the root Control
Region by its window owner, each child workspace by its workspace identity,
and the legacy window workspace by its panels-root identity. Floating layouts
inherit their owner's level. Thus a section can move within its level without
being tied to a physical side of the window; it cannot become child-workspace
content merely because it declares the Content trait.

The root Andamento sections and selector declare ControlSplitScope. Their
logical owner stays the root split, including when repairing an old saved
placement in a child workspace. Other Views use their source layout's level
for a move. New ordinary content belongs to its destination level. Creation,
move, split, drag feedback and restore consult the same checker. Nested
subject-rooted providers supply their owning split bindings in #164.

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
consult the model. Move and split execution remeasure the live client area and current layout.
The common resulting-width calculation uses serialized split fractions, pixel
rounding, insertion followed by emptied source Panel removal, sibling
redistribution, parent collapse and both panel insets; horizontal tab chrome
does not reduce body width. Drag targets use this same calculation. Restore
alone uses unmeasured geometry;
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

On Linux, `python tools/test-docking-integration.py <andamento-lib-dir>
<cleat-lib-dir>` exercises the real panel drag UI and move/split command routes
with a controllable OS size boundary. It covers 127/128/129px body widths, root
bisection, both parent axes, sibling insertion in all directions, and resizing
between feedback and commit. Last-View moves combine removal and insertion,
including same-axis sibling insertion, and compare feedback and command
acceptance with the final rendered body. CI native builds also run it through `build.sh`.

The runner accepts `--andamento-include <directory>` and `--cleat-include
<directory>` for non-sibling checkouts. Defaults honor `WHEELHOUSE_ANDAMENTO_DIR`
and `WHEELHOUSE_CLEAT_DIR`; `build.sh` passes the configured include directories.
Rendering and measurement share window-edge and panel-inset helpers. A drag
builds one geometry context from its existing mount; command measurement frees
its temporary panel tree before returning.

## Sidebar acceptance and integration boundaries


Projects, Sessions, Attention and Git are `sidebar_section` Views in the saved
`control_views` panel tree. The shared panel renderer chooses section headers
for one sidebar tab and compact tabs for merged sections. Ordinary workspace
content uses normal tabs. Placement creation, moves, drops and restore use the existing
`rd_dock_check` rules. No placement hints were added to KDL.

The human review clarified the level boundary: a View may dock only on its
owning Controlled Split level. Fleet sections use the root split's Andamento
state. They may rearrange within that level but cannot enter a selected child
workspace, including the legacy window workspace. Host presentation does not
grant permission to cross levels. Root-level floating placements remain valid;
floating placements inside a child workspace do not. This corrects #163's
original instruction to allow sections into workspace content. Nested split
providers and their bindings remain #164's work.

Restore recovers fleet sections moved into child workspaces by earlier previews
into a leaf of the root Control Region, keeping View identity, section key and
settings. They remain available across workspace switches. A recovered section
may share a compact-tab panel with an existing section.

The section key supplies the scroll identity across docking moves. Default
vertical panels retain content-based secondary sizing; dragging a panel boundary
switches the arrangement to saved split ratios. Double-clicking a sidebar
boundary or explicitly resetting panels returns to automatic content sizing.
Collapse state belongs to the View. Reveal clears the saved collapse marker before
scrolling to its workspace, so the section remains expanded on subsequent frames.
An invalid saved View without a section identity shows “Section unavailable”.
An empty saved sidebar remains empty after
restart. Initial creation waits for a valid snapshot containing sections, so a
startup failure cannot save an empty host. Regions added by later templates are
not inserted into an existing saved arrangement; placement hints and migration
policy remain #162's work.

The persistent footer's Sections menu restores intentionally closed sections
(#183); it remains outside the saved panels when all of them are closed.
Singleton section headers, compact tabs and ordinary tabs share the left-side
dotted grip and ten-point drag threshold (#184). The operator accepted the grips
in the 2026-10-06 kiwi review. Restored sections retain their declared header
controls. Protected chips use horizontal scrolling and reveal keyboard focus
when Tab/Shift-Tab enters a clipped chip or overflow button (#174). At widths
below the normal name budget, the name gives space to the chip viewport. Failed
display-value restore reconciliation is tracked separately in #185.

Boolean display controls use the declared glyph and tooltip label. A checked
button has the selection fill and border. Persistent values are stored by the
variable's stable identity under the window's `sidebar_display` node in the
saved user configuration. Panel resets replace the panels subtree and retain
these window values. Undeclared and `persist=false` variables are excluded. This
uses the additive Andamento ABI 2 accessor from andamento#125, without changing
core toggle behavior.

## Kiwi human review

Host-direct candidate runs now use `/tmp/sidebar-docking-review/Docking Reveal Review.app`
and disposable user/project settings. Each run included a call for human review.
The daily driver was left untouched. The first candidate exposed an origin
conversion error; the next run corrected it. The current candidate hides empty
sections, retains single-section headers and shows Issues, Show finished and
Role history beside the count. Human review exposed that fleet sections could
enter child workspaces and disappear from view when those workspaces changed.
The latest candidate enforces owning levels. Its copied arrangement visibly recovers Projects, Other workspaces and Attention into a fleet-level compact-tab panel.
A subsequent review fixed Reveal opening a collapsed docked section. The human
accepted rearranging sections, workspace switching and the fleet-level boundary
in the refreshed Docking Reveal Review candidate.

The human selected **Role history** as the replacement for Role attempts. The
shipped template uses that label; the stable variable identity, existing behavior
and persisted settings remain unchanged.

Reproduce the fixture with disposable settings:

```sh
./build/wheelhouse --sidebar_subject_fixture --user:/tmp/docking-user --project:/tmp/docking-project
```

Hover a header to expose drag and close. Drag sections to reorder them, merge
one into another panel, and check that child workspaces offer no section targets.
Switch workspaces and verify that fleet controls remain available. Try the
three display buttons and restart with the same disposable profile.

## Verification

The macOS debug build passes. Native panel diagnostics exercise the shared host
at a nonzero origin, alternating fractional wheel events between two overflowing
sections, independent offsets, idle-frame stability, bounds, singleton header
presentation, merge/split, rejected cross-level moves and splits, absence of
workspace drop affordances, and recovery of old saved placements. Docked-section
Reveal was reproduced failing on three consecutive frames before the saved
collapse-state override was corrected; its regression now passes. Header controls fit at 260 and 600 points; native press/release events toggle their values and
pressed appearance. Hover checks cover hidden and visible close controls.
Persistent values survive serializing and reloading the window configuration and
creating a new core; ephemeral declarations are skipped. Startup checks cover
missing snapshots followed by recovery, without saving an empty host. A real
boundary drag writes a manual allocation that survives configuration
serialization, and a double-click restores content sizing. Idle frames do not
dirty the configuration. Shared core preparation and observation run once per UI
build, with a fresh observation after actions. Section-specific geometry still
walks the snapshot; performance measurement and caching are tracked in #182.

The native sidebar ABI suite passes all 27 tests. Shared UI, terminal selection,
clipboard, sidebar, scroll-region, preview, terminal link, tooltip, managed-
content and terminal glyph diagnostics pass. Docking policy, generated fixture
checks and the embedded-fixture locale/newline test pass. The locale/newline
test uses Homebrew Python because the system Python predates its
`Path.write_text` API.

Display restore recovery (#185) performs at most three attempts: immediately,
after one second, and after two more seconds. Sleeping wake timers work in local
and live sidebars without requesting continuous frames; they carry no window,
state or snapshot pointers. A reference-counted completion token records a
fired worker independently of the deadline: an early wake consumes the token
and re-arms once, while unrelated polls do not spawn more timers. Tokens also
outlive retired sidebars without referring to them. Each attempt refreshes the
snapshot and resolves the declaration identity to its current action. A failed
preference is retained when other controls are saved; exhaustion keeps both
the saved intent and the surfaced error and performs no further dispatches.
Changing the saved target during recovery replaces the old intent, while a
new live user value saved by an explicit UI action supersedes recovery. Polling
never writes preferences from an externally changed live value. Explicit restoration or the next
session may start a new bounded attempt sequence. The headless command/drag
integration suite exercises failure then recovery, snapshot invalidation,
repeated failure across 1,000 polls, preservation on unrelated saves and newer
saved/live intent. Core toggle semantics are unchanged.

Explicit display restoration replaces the previous pending list. Its immediate
retry pass recomputes the earliest armed deadline from the replacement list;
retired timer tokens retain no sidebar or snapshot pointers. A declaration
missing from a refreshed snapshot keeps its saved intent and names that
identity in the error across the same three-attempt bound.

The integration runner intercepts only these process boundaries: WM client
rectangle, Andamento action dispatch, clock, worker creation and detach, sleep,
and WM wake posting. It links the real parser, settings, mount, panel renderer,
command routes, placement checker, snapshot refresh and retry worker. Review
new call sites at these boundaries when extending the production amalgamation;
otherwise they may bypass the deterministic harness adapters. The harness
uses release arena bookkeeping so native runs fit the 8 GiB address-space cap.
