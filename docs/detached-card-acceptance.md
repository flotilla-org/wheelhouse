# Detached hover cards

A card's controls live in its cap (#269): a strip the card's width and style,
joined to its top edge, or its bottom edge when there's no room above. It
overlays what it covers and takes the pointer from it, so nothing reflows. A
hover card shows its cap once the pointer moves into it, a pinned card or one
expanded under its row while the pointer is over it, and a floating card
always. The cap holds the entity's actions from Andamento as glyphs (their
labels are the tooltips; a narrow cap folds them into ⋯), Show details (ⓘ),
Pin on hover and floating cards, ⤡ back to the row on an expanded card, and
× elsewhere. Its controls are section-header controls: the same glyphs,
quiet style and minimum hit width. The card body is information only. There
is no Float or Dock under source control: a card floats or docks under its
source by dragging, and drags from its title line.
Pinned-area and card header rows use the ordinary section-header text size
and muted style. Descriptive tooltips use the main text font.
Dragging past ten points releases the card from its hover anchor. Sidebar drops
use the same target widgets and animated highlights as ordinary panel drags.
A drop over the source docks inline; a release without a selected panel target
floats over the workspace. All three destinations use `rd_dock_check` and the
registered card section's zero minimum width.

Floating cards stay open after mouse-out and source removal. Under source inserts
card content into the source row's scrolling tree, including project collapse
and section height allocation. An inline subject chip attaches beneath its owning
row. Removing the source placement or the current detail target closes the inline
card. Floats and inline cards last for the current run.

Pins are ghosts in local groups (docs/design/drag-model.md, "Sections and
groups as data"). Andamento owns the sections someone made, each holding
groups, and each group's ghosts with exact kind/id, a ghost id, a fallback
label and their form, as local `.section`, `.group` and `.ref` entities in its
dashboard record; Wheelhouse edits a working copy of them (`card` nodes in
uishell_sidebar_local_tree) and sends changes with `andamento_local_set`.
Local workspaces name their group (`lives_in`). The shipped KDL places them,
so each section is an ordinary docked `sidebar_section` View and its ghosts
are tree rows: a `.ref` presents its target's label, status, live state and
details. The
default Workspaces section and group always exist; leftover tabs join that
group. Saved `pinned_cards` areas migrate once into a section and group in
the View's place.

Section headers move the section through the shared docking system; compact
tabs appear when sections share a panel. A card dropped on a split target
creates a section and group through the ordinary panel split command,
including nested layouts. A centre drop joins the first group of a section
shown in that panel. The sidebar retains those split ratios instead of
applying automatic content sizing after the drop. Each pin is a ghost: its own
reference to the subject, so a subject may have any number of pins. Pin puts
the subject in the topmost local section that is showing (its panel's selected
tab), in its first group other than Workspaces; with none showing, it makes a
section at the top of the sidebar. A subject already pinned there is revealed
instead, and Pin reads Show pin. Dropping into a group is explicit placement
and always adds one. Moving a pinned card to another group keeps the same
saved card identity. A group emptied by a move remains, and its section's
Close control hides it.

Sections and groups you made are managed from menus: right-click a section's
title or hold its ×, and right-click a group's header. A right-click menu
opens at the pointer; a held × opens its menu at the ×. They offer Rename,
New group, New workspace here, Reset order once the group has been
reordered, Hide, and Delete. Rename, or double-clicking a title, swaps the
title for a field: Enter or clicking away applies, Esc cancels. Delete moves a group's
workspaces to Workspaces and drops its ghosts, asking first only when
workspaces would move; a section left without groups goes too. Workspaces
can't be deleted. A section without a name of its own shows its only group's
name, and renaming it renames the group; with several groups it shows their
names, joined. Dragging a group's header to a group in another section moves it
there; dropping it on a docking site makes it a section of its own. A
section showing one group is that group: its title dropped on another
section's group moves the group there, and the emptied section goes. Copied
or restored layouts reconcile copies of the same pin (same ghost id) within
the owning window. Pins saved before ghost ids keep the first per subject,
which gains an id.

Pinning a card keeps the card. A pinned card's header disclosure collapses it
to a compact ghost row, drawn as its home row is: the subject's icon, live
label and status, a ↗ marker for a reference that lives elsewhere, and a
disclosure that restores the card. Hovering it offers the subject's hover
card.
Rows and cards are one sidebar drag (`uishell_sidebar_drag_begin` and
`uishell_sidebar_drag_finish`), with the same targets. A dragged row lifts as a
translucent copy above the sidebar; a card moves itself. Over a local group,
either shows an insertion line between its items, and docking's centre and
catch-all sites stand aside (its edge sites still split). Releasing there: a
local workspace moves into the group (Option/Alt adds a ghost of it instead);
a pinned card, or a ghost's row, moves that ghost; anything else becomes a
ghost there, a row as a row and a card as a card. The group's order puts it at
that point. A docking site makes a new section holding the ghost. Over its own
sibling run a row reorders instead; elsewhere a row snaps back and a card
floats. The source row stays in its home and its siblings keep their order.
Clicking a ghost row goes to its source. The × in the row's right margin
removes the pin and never its source; holding it or right-clicking the row
opens Go to source, Show as card or row, and Remove pin.

A pinned card whose subject disappears remains visible with “No longer present”.
On 2026-10-06, the operator chose to retain ended cards until explicitly closed
in the #183/#184/#174 polish review (#202). No timeout applies, including after
restart.

Clicking a transient card still keeps it open until Escape or an outside click.
Detached cards keep their placement when Escape, outside clicks or window focus
loss release their keyboard focus. Escape consumes both edges when a card owns
focus. Close explicitly removes a pin from the saved layout. Card surfaces
suppress the whole-card focus overlay and border; focused child controls retain
normal focus feedback and keyboard activation. Related, Back, Details, semantic
actions and live previews use the existing card body.

## Kiwi review

Build disposable Near and Outside candidates on kiwi, each with separate user
and project settings, and request physical review for each run. Candidate paths,
run times, results and the ended-card timeout decision belong in the PR review
record. Leave the daily driver alone.

Try a subject card in both placements: click it, leave it, Tab and Shift-Tab
through its controls, then dismiss with Escape and an outside click. Open Example
terminal or Example workspace to inspect the live preview. Drag a card into the
workspace, under its source, and elsewhere in the sidebar. Scroll and collapse
the tree with an inline card. Pin the same entity twice, move its area, filter the
tree, close a pin, and restart with the same disposable profile.

The operator accepted the revised Near styling and confirmed the workspace
section drop to the top of the sidebar and pinned-card spacing at `10459f5`.
Broader placement acceptance remains with the operator. The operator selected
retention until explicit close on 2026-10-06 (#202).

## Automated evidence

The macOS debug build uses CI-pinned Cleat `00c072b`, Andamento `08315d2` and
Jackstay `91156bf`. Docking validity, generated-source checks, the embedded-fixture
locale/newline test and all 28 native sidebar ABI tests pass. The ten native
diagnostic groups pass: shared UI, terminal selection, sidebar, scroll region,
preview, terminal links, tooltip, panel, managed content and terminal glyphs.

The hover-card diagnostic drives the real cap controls and the title drag
handle through press/motion/release frames (it drove the Float control and
grip before the cap replaced them). It covers detached mouse-out persistence,
Escape press/release, outside-click focus return, neutral surfaces, child focus
feedback, inline measured height, source removal, the pinned View's measured body
and clipped hit geometry, duplicate-pin reveal, moving between areas, copied-layout
deduplication, saved-layout serialization/reload, missing-subject display, explicit
pin removal, and preservation of a merged sidebar when another area is added.
Center and four-direction card drops exercise the shared panel callback, ordinary
split command, exact destination side and saved split sizing.
Saved-layout tolerance covers unknown entity kinds, missing identity fields,
extra fields and duplicate identities through reconciliation, serialization and
actual pinned View rendering. Unknown or incomplete entries remain closable with
the missing-subject marker.

The native card diagnostics also cancel a live drag through raw Escape, clear
creation callbacks when the owning window is torn down, reject tentative
creation with and without an existing root, and execute queued directional
drops into nested panels while preserving the neighbour and outer allocation.

Translated pinned-View parents retain a six-point inset and eight-point gaps
between two cards, independent of the panel's window offset. An Escape-cancelled
creation drag followed by a normal tab drag queues an ordinary move. A workspace
section drop exercises split dispatch and empty-source cleanup in a nested
layout; flattening preserves the surviving leaf allocations and restores focus
to a live leaf rather than the released split container.

Center drops cover a transient copy of the same pinned entity, the pin dropped
into its own area, and a different entity appended alongside the existing pin.
The same-area cases retain one saved identity and do not relink it after itself.
Former pins floated and dragged through the actual title handle remain
stationary after raw mouse release, Escape cancellation, and window focus loss.
These raw events clear both the card drag owner and its UI handle before the
next build.
