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

Near is the default placement. It opens below the source row with a 16-point gap, overlapping the
sidebar edge. When there is insufficient space below, it uses the space above.
The gap leaves adjacent pills reachable while the safe corridor still protects
diagonal entry.
**Hover Cards Outside Sidebar** in User Settings selects the fully outside
placement. `--hover_cards_outside` forces it for a disposable candidate.
Both placements clamp to the client rectangle; long cards use the shared
scroll region so actions and details remain reachable.

Separate candidate applications and settings live under
`/tmp/wheelhouse-hover-review/`. The daily driver has not been restarted or
replaced. On 2026-10-04, after reviewing the refreshed candidates on kiwi,
the operator accepted phase 1: “That looks good. Lets default to near”.
Near remains the default; Outside remains available through User Settings.
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

## Structured cards (#177)

Wheelhouse now opts into Andamento's `snapshot_acquire_details` at revision
`9718ba15b8f29cf1379d6f4f11320d30dd953fde` (Andamento #126). The native role
agreement is recorded on [Andamento #124](https://github.com/flotilla-org/andamento/issues/124#issuecomment-5985487611).
The earlier placement-edge Related fallback above is superseded by typed
relation fields.

The header uses kind icons, identity, title and a state badge. Facts occupy two
columns with icons and values. The engaged header has a Details toggle that
reveals full fact labels, controller-relative observation ages and stale styling.
The actions footer packs its controls into one row. Missing
facts are omitted; known-empty facts show a dash. Retained stale values
and ages are dimmed in Details mode. Producer timestamp strings are never parsed. Related
mini-rows show kind icons, target labels and state chips. An unavailable target
is informational; available targets navigate by exact kind/id, including those
absent from the tree. All path entities and duplicate targets are omitted.

The existing live preview stays in both card states. Engaged cards expose the
semantic controls supplied by Andamento in an actions footer, plus Back and
Close. Pending actions retain entity identity and semantic intent, and resolve
against the snapshot current at sidebar dispatch. A disappearing or changed
intent cancels the pending action. Near remains the default and Outside remains
in User Settings. The existing timing, safe corridor and focus controller is
unchanged.

Disposable kiwi candidates for this slice live under
`/tmp/wheelhouse-structured-hover-review/`: **Structured Cards Near.app** and
**Structured Cards Outside.app**. Their launchers supply separate disposable
user/project settings and the subject fixture. The daily driver is untouched.
Both candidates were launched for human review on 2026-10-05
(Europe/London; 2026-10-04 UTC). The operator
confirmed that the live previews work and requested a more compact, glanceable
layout. The operator asked to hide fact labels and freshness by default after seeing
the compact layout, since the labels did not fit. The latest revision uses
icons and values by default and reveals labels and freshness with Details.
The operator accepted this revision: “That looks better.” The whole card turns
blue when clicked; that retained phase-1 focus styling is tracked separately in
#187, as the operator allowed a follow-up.

The fixture exposes change requests, issues, convoys, roles and projects.
The project's Related rows reach a worktree and an issue with no tree placement.
The PR's Related rows include a missing target and a cycle. Review all six
kinds in both placements, Related/Back, modifier-open, rapid source swaps,
diagonal entry, click focus, outside dismissal and Escape. Open Example terminal
or Example workspace and check its live preview in peek and engaged states.
Native synthetic pointer input does not establish physical hover acceptance.

The tooltip diagnostic exercises the production body and controller, including
all six shipped templates, hidden catalog targets, path omission, modifier-open,
relative ages, known-empty facts, retained stale styling, preview identity and
snapshot-refresh action cancellation. Existing timing and input ownership
traces remain in place.
