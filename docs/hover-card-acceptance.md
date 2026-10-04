# Hover cards: phase 1 acceptance

Phase 1 of #165 keeps the flat `<kind>/detail` fields and adds a persistent
card controller. The first source waits 300 ms; subsequent rows and chips swap
immediately, retaining the previous content during a 100 ms cross-fade and
an ease-out glide. Position comes from the source rectangle, not the pointer.
A triangle toward the approaching side or top/bottom edge protects diagonal
travel for up to 400 ms, including northwest/southeast target arrangements. Vertical scanning and movement away release that protection.

Entering a card engages it and reveals related navigation and actions.
Mouse-out closes an unfocused card after 400 ms. A click gives the card keyboard
focus and keeps it open until an outside click or Escape. Hover alone leaves
Escape with the focused View; a focused card consumes both Escape edges and
returns focus to that View. An outside click also activates its normal target.
Underlying sidebar and workspace controls do not
receive pointer hits through a card. A removed or clipped source closes its
card, as does removal of the current detail target. Hiding the sidebar closes
both cards before their controls can emit an action. Native window focus loss
dismisses click-focused cards and releases Escape ownership. Informational
cards remain available to hover while another window owns keyboard focus,
including during screen recording. Pointer tracking continues until mouse-out
closes the card; keyboard input remains with the active window.

Related navigation uses the snapshot's placement parents and direct children,
including aliases in other sections. It deduplicates entities and omits every
entity already on the path. Back restores the previous target. Cmd-click or Ctrl-click opens a second card without changing the
original path. This second transient card captures its launch anchor and stays
in place when the original navigates or closes. It supports click focus and its own
navigation; detaching, dragging and pinning remain #166.

Live entities keep the existing workspace preview demand, cached surface and
custom drawing path in both peek and engaged states. This dispatch does not
change preview attachment policy (#89).

## Placements and kiwi review

Near placement opens below the source row with a 16-point gap, overlapping the
sidebar edge. When there is insufficient space below, it uses the space above.
The gap leaves adjacent pills reachable while the safe corridor still protects
diagonal entry.
**Hover Cards Outside Sidebar** in User Settings selects the fully outside
placement. `--hover_cards_outside` forces it for a disposable candidate.
Both placements clamp to the client rectangle; long cards use the shared
scroll region so actions and details remain reachable.

Separate candidate applications and settings live under
`/tmp/wheelhouse-hover-review/`. The daily driver has not been restarted or
replaced. Native human acceptance and the final placement choice are pending.
The automation tool's synthetic pointer does not update the native global
mouse position that Wheelhouse reads, so its synthetic hover is not evidence
of physical pointer acceptance.

To reproduce with disposable settings:

```sh
./build/wheelhouse --sidebar_subject_fixture \
  --user:/tmp/hover-near-user --project:/tmp/hover-near-project
./build/wheelhouse --sidebar_subject_fixture --hover_cards_outside \
  --user:/tmp/hover-outside-user --project:/tmp/hover-outside-project
```

Check the first delay, rapid scanning, diagonal travel and engagement; click
inside a card, leave it, then dismiss with Escape or an outside click. Navigate
through a convoy's subjects and Back; modifier-click a related item for a
second card. Open Example terminal or Example workspace, then hover its source
to check the live preview in both states. With a terminal focused, a peek card
must leave Escape with the terminal. Click-focus a card, then use Tab (and
Shift-Tab) or arrows through Related, Back and the action buttons; Enter should
activate the highlighted action.

## Automated verification

The macOS debug candidate uses CI-pinned Cleat `00c072b`, Andamento `b72a103`
and Jackstay `91156bf`, with a prepared Ghostty library. Ten native diagnostic
groups pass: shared UI, terminal selection, sidebar, scroll region, preview,
terminal links, tooltip, panel, managed content and terminal glyphs. The
existing 27 native ABI/width tests and generated-fixture locale/newline test
pass, and generated sources match.

The tooltip diagnostic also runs hover-card checks through production seams: 300
ms opening, immediate replacement and outgoing content, diagonal and stationary
corridor traces, 400 ms closing, click focus, Escape press/release, outside
dismissal and target activation, overlapping two-card hit order, focus loss
while Escape is held, long paths, Related/Back widget activation, independent
closure of the second card, path retention and exclusion of underlying pointer
events, including a batched click whose final pointer position is elsewhere.
Full controller layout checks ensure that fields and controls occupy separate
rows and that initial bounds include the measured content. Cross-fade layers
share the same origin. Actual body construction checks flat title fields,
engagement-only actions, preview boxes and live preview demand in both states.
These tests do not establish physical native acceptance. Card actions retain
only a stable target key and action kind until sidebar dispatch; a snapshot-
refresh trace checks both valid resolution and cancellation when the selected
action disappears.

## Phase 2

[Andamento #124](https://github.com/flotilla-org/andamento/issues/124) adds
section/role metadata, separate fact labels and observation times, related
entity targets and semantic actions. ABI 2 exposes resolved text and placement
edges today; it cannot express the full relation graph or a detail target absent
from the current tree. The renderer must not infer those roles by parsing text.

[Wheelhouse #177](https://github.com/flotilla-org/wheelhouse/issues/177) then
builds the header, badge, two-column facts with relative ages and stale styling,
related mini-rows and actions footer for change requests, issues, convoys,
roles, projects and worktrees. It keeps phase 1's interaction and live preview.
