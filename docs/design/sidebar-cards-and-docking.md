# Sidebar rows, hover cards and docking

Design direction from the 2026-10-04 grilling, now that Flotilla publishes
subjects, readiness and standing roles. Nothing here is built yet. Each section
is tracked by a ticket listed at the end; expect the presentation details to
move once there is something on screen.

## Rows carry their subjects as chips

#148 placed a convoy's PRs and issues as child rows under the convoy. That was
an interim shape. A convoy row should instead carry them as compact chips:

```
[icon] fix-scroll-regre…   !281 ● #120 ○  +2   [status]
```

- One chip per subject, using the compact reference (`!281`, `c!281`, `#120`)
  from flotilla ADR 0049. Readiness is colour or a small glyph, not words.
- Hovering a chip shows that subject's card; clicking opens its URL, as the
  child rows do today.
- Subject chips and workspace-action chips (#79) share one chip area with one
  `+N` overflow. Workspace actions should become fewer as more of them are
  projected into larger views, for example the overview TUI and a project's
  standing roles combining into one project-level workspace.
- Closed and merged subjects follow Show finished, as now. Attention keeps
  standalone PR rows, since there the PR itself is what needs action.

Row anatomy, left to right: kind icon, name, chip area, the fixed trailing
status slot (`andamento-native-reconciliation.md`). Under width pressure the
name gives way first, through the producer's `display.label.medium` and
`display.label.short` forms (andamento#68) and then an ellipsis, down to a
minimum of about ten characters. After that, quiet chips (an issue being worked
on, a PR awaiting review) fold into `+N`. Attention-bearing chips (ready to
merge, CI failing, an open workspace with activity) never fold. Widening the
sidebar or hovering shows the rest.

## Hover cards

### Content

A card has a fixed structure instead of a list of `Prefix: value` lines:

- **Header:** kind icon, identity, title, and a state badge.
- **Facts:** a two-column grid of short pairs (`CI ✓`, `Review: approved`).
  Each fact's `observed_at` becomes a small relative age beside it; stale facts
  are dimmed rather than given their own line.
- **Related:** linked entities as clickable mini-rows with their own chips,
  leaving out anything already on the navigation path. A PR card reached from
  its convoy does not list the convoy or project.
- **Preview:** the live workspace preview, when the entity has a workspace, as
  today's hover card draws it. The restructure keeps it; #89 decides when a
  preview may attach.
- **Actions:** icon buttons from the same Andamento controls as the workspace
  controls (#122): open in browser, attach, copy URL.

Card content stays template-defined (`<kind>/detail`, slot `detail`), but
fields declare their section and role (badge, fact, age, related) rather than a
prefix string. Wheelhouse owns the visual style.

### Peek and engaged

A card has two states. A **peek** card is information only: no actions, no
drag handles, no keyboard focus. Moving the pointer into it makes it
**engaged**, and only then do actions and move controls appear.

- The card anchors to the edge of the row or chip it came from and does not
  follow the pointer. Prototype both placements: just past the hovered element,
  overlapping the sidebar edge, and fully outside the sidebar.
- The first card appears after about 300 ms. After that, hovering another row
  or chip swaps content at once; the card glides to its new anchor (about
  100 ms, ease-out) and cross-fades.
- A safe corridor from the source towards the card stops a diagonal move into
  it from switching rows on the way.
- Leaving an engaged card closes it after about 400 ms unless it has been
  dragged or pinned.
- Escape follows keyboard focus. A peek card never takes focus, so Escape
  reaches the terminal or whatever else holds it. A card handles Escape only
  after a click has focused it.
- Clicking a related item navigates within the card, with a back arrow. A
  modifier, or dragging the item out, opens it as its own card. Later, each
  card segment may be dragged out separately with the same gesture.

### Detached cards

Dragging or pinning an engaged card detaches it. It can then:

- **float** over the workspace (the default after a drag);
- **dock under its source**, expanding inline beneath the row, scrolling with
  the tree and going away with the row's subject; or
- move to the **pinned area**, which survives scrolling, filtering and
  restarts.

The pinned area is an ordinary sidebar section, so where it lives is
configurable like any other section. #89 owns attachment policy for previews
inside cards.

## Sidebar sections are docked views

Sidebar sections (Projects, Sessions, Attention, Git, Pinned) become Views in
the existing docking system rather than a second layout mechanism. The host a
panel sits in decides how it looks:

- In the sidebar, a single-tab panel shows a section header (title, count, the
  display toggles from #80) instead of a tab strip. Drag and close appear on
  hover, following the same peek and engaged idea as cards.
- Two sections in one panel show compact tabs. A section dragged into the
  workspace becomes an ordinary panel with the normal tab look.

The docking system will grow: stacked sections that scroll as one area, and
per-host choices of tab presentation, including in the main workspace. Those
are enrichments of one system; none of them justify a duplicate.

### Docking validity

What may dock where is declared, never decided in gesture code:

- Views declare traits, for example *selects workspaces*, *singleton*, *needs
  a workspace subject*, *minimum width*, *stacks as a section*.
- Hosts declare what they accept: the sidebar takes section-style views, a
  Workspace Region takes content views, a floating panel takes anything that
  does not need a host.
- Structural invariants come from `CONTEXT.md`. A Controlled Split has exactly
  one Control Surface. A Workspace Region cannot contain the Control Surface
  that selects it. The workspace-selecting view can be moved but not closed.

One checker serves three callers: drag feedback (invalid targets never appear),
layout restore (invalid placements fall back to their default), and tests that
enumerate view and host pairs. Validity is a property of the view and the host,
not of the gesture.

The foundation is described in [Declared docking validity](docking-validity.md).

### KDL owns what, layout owns where

- An Andamento `region` declares a section's identity and content plus a
  default placement hint, such as `default-host="sidebar" order=10`.
- Wheelhouse's RAD-derived layout state stores the actual arrangement, keyed by
  region id.
- A region with no saved position goes to its hint. A saved position for a
  region that no longer exists is dropped. A reset command clears the saved
  arrangement back to the hints.

Drags are never written back into KDL, which is shared across hosts (Zellij
and Wheelhouse) and producers. Editing the KDL from Wheelhouse may come later
as an explicit action that shows the source.

## Nested splits are recursive Controlled Splits

A convoy or project workspace may hold a second layer that selects a sub-item,
for example which vessel of a convoy to look at. This is a Controlled Split
inside a Workspace Region:

- The inner Control Surface is an Andamento region rooted at the Workspace
  Subject (#122): a convoy's vessels, or a project's convoys and standing
  roles. It may be a list, a strip, or later something richer, such as a graph
  of a large convoy's agents and their interactions.
- Selecting a child mounts that child's sub-workspace from its Suggested Layout.
  Each sub-workspace keeps its state across switches.
- The validity checker gains its first nested invariant: an inner Control
  Surface selects only children of its own workspace's subject.

A single panel whose view retargets through a picker is a cheaper first
prototype, but it loses per-child layout. The recursive split is the target.

## Prototype findings

`sidebar-cards-prototype.html`, next to this doc, is a throwaway web mockup of
rows, chips, hover cards and detaching, with live controls for placement,
timings and the safe corridor. Open it directly in a browser. It is kept as a
reference for an eventual web Flotilla/Wheelhouse surface. Operator verdicts
from 2026-10-04:

- **Near placement wins**, with outside-the-sidebar kept as a preference. Near
  covers the rest of the hovered row's chips; the corridor and the instant
  swap make that acceptable in use.
- **Fold order reads fine.** At the default width the first convoy's name
  drops to its short form because attention chips hold their space; that is
  acceptable.
- **A card focused by a click stays open** until a click elsewhere or Escape.
  Without this, the card holding focus would vanish on mouse-out.
- **A pinned card outlives its subject** and says it is no longer present.
  Ended presentations and cards will need a timeout policy; it is not decided.
- **One pinned card per entity.** Pinning again reveals the existing card.
- **Pinned areas need not be singular.** Since pinned areas are ordinary
  sections in the docking system, dragging a card to a new place in the
  sidebar could create a new pinned area there.

## Tickets

- #160 Convoy rows carry PR and issue subjects as compact chips (with #79)
- #165 Structured hover cards with peek and engaged states (with #89)
- #166 Detach hover cards to float, dock under their source, or pin
- #163 Sidebar sections as docked Views with host-styled panels (with #80)
- #161 Declared docking validity
- #162 Sidebar sections: KDL declares what, Wheelhouse layout stores where
- #164 Nested splits as recursive Controlled Splits (after #122)

Unopened workspace actions enter `+N` before a quiet issue chip (owner ruling,
2026-10-05, #175). The action remains reachable from the project row and hover
card; the issue chip carries state only this row shows. Numeric PR/issue order
is retained. Attention-bearing chips never fold and the trailing status slot
never moves. For a narrow mixed row with an 80px name, 60px unopened action,
50px quiet issue and 30px overflow, a 160px content budget shows the issue and
`+1`; the unopened action goes into overflow even when it precedes the issue.
