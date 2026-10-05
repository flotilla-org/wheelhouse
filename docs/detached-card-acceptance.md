# Detached hover cards

Engaged cards expose Drag, Pin, Under source and Float. Dragging past ten points
releases the card from its hover anchor. The drop label shows the destination:
Float over the workspace, Dock under source over the visible source, or Pin here
elsewhere in the sidebar. All three destinations use `rd_dock_check` and the
registered card section's zero minimum width.

Floating cards stay open after mouse-out and source removal. Under source inserts
card content into the source row's scrolling tree, including project collapse
and section height allocation. An inline subject chip attaches beneath its owning
row. Removing the source placement or the current detail target closes the inline
card. Floats and inline cards last for the current run.

Pinned areas are ordinary `pinned_cards` Views in `control_views`. Their headers
move the area through the shared docking system; compact tabs appear when areas
share a panel. A card dragged elsewhere in the sidebar creates an area at that
position in the root arrangement. Existing merged tabs and panel proportions are
preserved. Pins save exact kind/id and a fallback label in the RAD-derived layout,
without changing KDL. Pinning an entity again selects and scrolls to its existing
card. Moving it to another area keeps the same saved card identity. Copied or
restored layouts reconcile duplicate pins within the owning window.

A pinned card whose subject disappears remains visible with “No longer present”.
The candidate does not automatically expire these cards. The human review asks
whether to keep them until explicitly closed or introduce a timeout; that policy
is still awaiting a decision.

Clicking a transient card still keeps it open until Escape or an outside click.
Detached cards keep their placement when Escape, outside clicks or window focus
loss release their keyboard focus. Escape consumes both edges when a card owns
focus. Close explicitly removes a pin from the saved layout. Card surfaces
suppress the whole-card focus overlay and border; focused child controls retain
normal focus feedback and keyboard activation. Related, Back, Details, semantic
actions and live previews use the existing card body.

## Kiwi review

The disposable Near and Outside candidates live at:

- `/tmp/wheelhouse-detached-card-review/Detached Cards Near.app`
- `/tmp/wheelhouse-detached-card-review/Detached Cards Outside.app`

Each has separate user and project settings under the same directory. The daily
driver was left alone. The first run was opened on 2026-10-05 with an explicit
call for physical review. A second run at 11:35 BST uses the final native-tested
build and includes another call for human review. Human acceptance is pending; synthetic UI input does
not establish native hover acceptance.

Try a subject card in both placements: click it, leave it, Tab and Shift-Tab
through its controls, then dismiss with Escape and an outside click. Open Example
terminal or Example workspace to inspect the live preview. Drag a card into the
workspace, under its source, and elsewhere in the sidebar. Scroll and collapse
the tree with an inline card. Pin the same entity twice, move its area, filter the
tree, close a pin, and restart with the same disposable profile.

## Automated evidence

The macOS debug build uses CI-pinned Cleat `00c072b`, Andamento `9718ba1` and
Jackstay `91156bf`. Docking validity, generated-source checks, the embedded-fixture
locale/newline test and all 27 native sidebar ABI tests pass. The ten native
diagnostic groups pass: shared UI, terminal selection, sidebar, scroll region,
preview, terminal links, tooltip, panel, managed content and terminal glyphs.

The hover-card diagnostic now drives the real Float control and Drag control
through press/motion/release frames. It covers detached mouse-out persistence,
Escape press/release, outside-click focus return, neutral surfaces, child focus
feedback, inline measured height, source removal, the pinned View's measured body
and clipped hit geometry, duplicate-pin reveal, moving between areas, copied-layout
deduplication, saved-layout serialization/reload, missing-subject display, explicit
pin removal, and preservation of a merged sidebar when another area is added.
