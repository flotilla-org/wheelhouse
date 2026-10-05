# Detached hover cards

Engaged cards expose a dotted drag grip at the left of the header, with compact
Show details, Float, Dock under source, Pin and Close icons at the right. Pinned-area and card header rows use the ordinary section-header text size
and muted style. Section and card grips use the same dotted symbol at the
left; entity and action icons use the sidebar rows’ muted styling. Descriptive tooltips use the main text font. Hover
an icon for its label. Detached cards omit their current destination icon.
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

Pinned areas are ordinary `pinned_cards` Views in `control_views`. Their headers
move the area through the shared docking system; compact tabs appear when areas
share a panel. A card dropped on a split target creates an area through the
ordinary panel split command, including nested layouts. A center drop joins an
existing pinned area or creates a tab in that panel. The sidebar retains those
split ratios instead of applying automatic content sizing after the drop.
Existing merged tabs are preserved. Pins save exact kind/id and a fallback label
in the RAD-derived layout,
without changing KDL. Pinning an entity again selects and scrolls to its existing
card. Moving it to another area keeps the same saved card identity. An area emptied
by a move remains available for another pin, with a “Drag or pin a card here”
placeholder; its ordinary section Close control removes the area. Copied or
restored layouts reconcile duplicate pins within the owning window.

A pinned card whose subject disappears remains visible with “No longer present”.
The proposed policy retains ended cards until explicitly closed. The acceptance
review must record whether to keep that policy or introduce a timeout.

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

## Automated evidence

The macOS debug build uses CI-pinned Cleat `00c072b`, Andamento `08315d2` and
Jackstay `91156bf`. Docking validity, generated-source checks, the embedded-fixture
locale/newline test and all 28 native sidebar ABI tests pass. The ten native
diagnostic groups pass: shared UI, terminal selection, sidebar, scroll region,
preview, terminal links, tooltip, panel, managed content and terminal glyphs.

The hover-card diagnostic now drives the real Float control and Drag control
through press/motion/release frames. It covers detached mouse-out persistence,
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
