# Sidebar docking: acceptance

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

Restoring intentionally closed sections is the human-requested follow-up #183.
Consistent movement affordances for section headers and tabs are #184. Failed
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
new live user value supersedes recovery. Explicit restoration or the next
session may start a new bounded attempt sequence. The headless command/drag
integration suite exercises failure then recovery, snapshot invalidation,
repeated failure across 1,000 polls, preservation on unrelated saves and newer
saved/live intent. Core toggle semantics are unchanged.
