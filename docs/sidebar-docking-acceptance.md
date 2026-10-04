# Sidebar docking: acceptance

Projects, Sessions, Attention and Git are `sidebar_section` Views in the saved
`control_views` panel tree. The shared panel renderer chooses section headers
for one sidebar tab, compact tabs for merged sections, and normal tabs in a
Workspace Region. Placement creation, moves, drops and restore use the existing
`rd_dock_check` rules. No placement hints were added to KDL.

The section key supplies the scroll identity across docking moves. Default
vertical panels retain content-based secondary sizing; dragging a panel boundary
switches the arrangement to saved split ratios. Collapse state belongs to the
View. An empty saved sidebar remains empty after restart.

Boolean display controls use the declared glyph and tooltip label. A checked
button has the selection fill and border. Persistent values are stored by the
variable's stable identity in the existing user configuration; undeclared and
`persist=false` variables are excluded. This uses the additive Andamento ABI 2
accessor from andamento#125, without changing core toggle behavior.

## Kiwi human review

Host-direct candidate runs use `/tmp/sidebar-docking-review/Sidebar Docking.app`
and disposable user/project settings. Each run included a call for human review.
The daily driver was left untouched. The first candidate exposed an origin
conversion error; the next run corrected it. The current candidate hides empty
sections, retains single-section headers and shows Issues, Show finished and
Role attempts beside the count. Human interaction feedback is still pending.

The human naming choice proposes **Role history** for what Role attempts reveals,
or grouping it with Show finished as history/detail controls. The shipped label
remains **Role attempts** pending that choice.

Reproduce the fixture with disposable settings:

```sh
./build/wheelhouse --sidebar_subject_fixture --user:/tmp/docking-user --project:/tmp/docking-project
```

Hover a header to expose drag and close. Drag sections to reorder them, merge
one into another panel, or move them into a Workspace Region and back. Try the
three display buttons and restart with the same disposable profile.

## Verification

The macOS debug build passes. Native panel diagnostics exercise the shared host
at a nonzero origin, alternating fractional wheel events between two overflowing
sections, independent offsets, idle-frame stability, bounds, singleton header
presentation, merge/split, transfers between hosts and restore. Header controls
fit at 260 and 600 points; native press/release events toggle their values and
pressed appearance. Hover checks cover hidden and visible close controls.
Persistent values survive a new core instance; ephemeral declarations are skipped.

The native sidebar ABI suite passes all 27 tests. Shared UI, terminal selection,
clipboard, sidebar, scroll-region, preview, terminal link, tooltip, managed-content
and terminal glyph diagnostics pass. Docking policy, generated fixture checks
and the embedded-fixture locale/newline test pass. The locale/newline test uses
Homebrew Python because the system Python predates its `Path.write_text` API.
