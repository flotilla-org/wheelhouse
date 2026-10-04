# Wheelhouse roadmap

Current as of 4 October 2026. This page is the entry point for Wheelhouse's
direction and work order. [Map issue #17](https://github.com/flotilla-org/wheelhouse/issues/17)
holds the broader destination and decisions; the linked issues hold acceptance
criteria. The design and validation notes below explain the current implementation.

## Current shape

Wheelhouse is a native workspace application built from tabbed panels and
embedded views. Cleat supplies terminal sessions and rendering data. Jackstay
supplies video and optional cooperative input to a panel. The workspace sidebar
uses Andamento's shared core and templates. It can run with local workspaces or
receive facts through HTTP over a Unix socket (a named pipe on Windows) from
the native Andamento git watcher and `flotilla pm connect`. Flotilla is the first
live fleet producer, not a dependency of the sidebar core. Wheelhouse owns workspace activation and layout.

The project tree is now the only workspace selector. It covers local and
provider-backed workspaces, supports Reveal and Close from the toolbar, and shows
existing workspace previews on hover. A selected vessel can appear inline on a
convoy row; project rows have room for several workspace actions. These are
presentation choices over stable entity and workspace identities.

## Landed since the previous roadmap

The toolbar breadcrumb (#47), standing project-role actions (#48), and complete
project repository membership (Flotilla #1897) have landed. Managed workspaces
reconcile one primary terminal against a stable opening intent; see
[managed primary content](design/managed-primary-content.md).

Terminal fixes now include selection lifetime (#72, PR #154), rectangular
selection (#73), wrap-aware Copy (#151, PR #153), hyperlinks (#77), and Shift
scrolling/middle-Paste overrides (#70, PR #156). Cursor flicker (#75), the
stopped/disconnected lifecycle design (#88), and stable-handle hosting transfer
(#81) are closed. The shared stopped-document provider API remains open in
[Cleat #277](https://github.com/flotilla-org/cleat/issues/277). Shift override
physical acceptance is still recorded as pending in
[its checklist](shift-override-acceptance.md).

Convoy PR/issue subjects and role history landed in PR #148; PR #155 restores
standing roles as project-row actions while history is hidden. Daily-driver
binary selection (#30) now defaults to the installed fleet. New terminal
processes discard inherited runner/agent and colour override flags (PR #154).

## Current work order

This is the maintenance order, not a claim that every investigation is ready to
implement. Later design items below are deferred, not blockers for current fixes.

1. **Finish terminal reliability follow-through.**
   [Cleat admission adoption #97](https://github.com/flotilla-org/wheelhouse/issues/97)
   remains open for rollout/acceptance evidence. The current Cleat pin already
   includes output-cycle admission; do not describe it as an unimplemented pin
   bump. Daemon/client and containing-session restarts remain a coordinated
   boundary. The [stopped/disconnected lifecycle note](design/stopped-and-disconnected-terminals.md)
   records the design; Cleat #277 owns the remaining shared lifecycle API.
2. **Complete clipboard and edit commands through the current attach path.**
   [Edit menu #152](https://github.com/flotilla-org/wheelhouse/issues/152) and
   [OSC 52 clipboard #71](https://github.com/flotilla-org/wheelhouse/issues/71)
   remain open. The [terminal clipboard review](design/terminal-clipboard.md)
   records the landed selection/copy fixes, ownership rules and delivery order.
   Cleat #240's shared clipboard transport has merged as Cleat PR #304; #71
   must adopt that ABI/protocol boundary and implement the desktop consumer.
   These should not wait for native remote attachment. Ghostty-style tracked
   history selection remains deferred design in Cleat #300.
3. **Continue sidebar presentation and explanations.**
   [Compact display controls #80](https://github.com/flotilla-org/wheelhouse/issues/80)
   and [unavailable-attachment explanations #69](https://github.com/flotilla-org/wheelhouse/issues/69)
   remain open. The [sidebar rows, cards and docking design](design/sidebar-cards-and-docking.md)
   records the accepted next direction and its tickets: compact subject chips,
   structured hover cards, docked section Views and recursive Controlled Splits.
   Keep shared interpretation in Andamento and native geometry in Wheelhouse.

The [Windows map #53](https://github.com/flotilla-org/wheelhouse/issues/53)
tracks that work separately, including remaining packaging, emoji and input
issues. Linux clipboard targets/PRIMARY/large transfers remain tracked in
[#150](https://github.com/flotilla-org/wheelhouse/issues/150).

## Reliability work alongside features

[Preview isolation #11](https://github.com/flotilla-org/wheelhouse/issues/11)
and [terminal dirtiness #9](https://github.com/flotilla-org/wheelhouse/issues/9)
remain open despite narrower fixes. Recheck them before expanding preview use.
[Sidebar context scans #29](https://github.com/flotilla-org/wheelhouse/issues/29)
are a performance follow-up. [Window/tab geometry #91](https://github.com/flotilla-org/wheelhouse/issues/91)
requires fresh screenshots before classifying the older observations as current
bugs; [GPU regression coverage #41](https://github.com/flotilla-org/wheelhouse/issues/41)
tracks thin borders and popup blur.

Other open defects and CI work remain on the
[Wheelhouse issue tracker](https://github.com/flotilla-org/wheelhouse/issues).

## Upstream ownership

- [Cleat #240](https://github.com/flotilla-org/cleat/issues/240) owns OSC 52 effect
  transport, merged in Cleat PR #304; Wheelhouse #71 owns pin adoption and desktop
  integration. The provider ABI moves from 10 to 11 and packet protocol from 11
  to 12, rejecting older counterparts.
- [Cleat #241](https://github.com/flotilla-org/cleat/issues/241) owns nested
  attachment scrollback semantics; Wheelhouse #70 delivered the outer native
  override. That does not expose all inner Cleat history.
- [Cleat #242](https://github.com/flotilla-org/cleat/issues/242) is closed with
  the synchronized-output fix; Wheelhouse #75 is also closed.
- [Flotilla #1961](https://github.com/flotilla-org/flotilla/issues/1961) owns
  readiness/blocker publication; Wheelhouse #69 consumes it through Andamento.
- Flotilla [#1975](https://github.com/flotilla-org/flotilla/issues/1975) and
  [#1976](https://github.com/flotilla-org/flotilla/issues/1976), terminal identity
  and crew Ghostty terminfo, are closed. Deployment into an existing crew is
  separate from merged source; do not diagnose remaining styling from TERM alone.

## Later decisions

| Investigation | Scope |
| --- | --- |
| [Direct remote Cleat views #104](https://github.com/flotilla-org/wheelhouse/issues/104) | Implementation following the completed transport investigation #21, without duplicate VT state. |
| [Preview policy and cards #89](https://github.com/flotilla-org/wheelhouse/issues/89) | Hydration, cached/shared previews, interactive cards and pinning. |
| [Workspace composition #90](https://github.com/flotilla-org/wheelhouse/issues/90) | Convoy overviews, dynamic managed content, contextual new views, naming and cross-project compositions. |
| [Accessibility #92](https://github.com/flotilla-org/wheelhouse/issues/92) | Assess native accessibility and a first integration slice. |
| [Jackstay discovery #93](https://github.com/flotilla-org/wheelhouse/issues/93) | Source selection while preserving explicit endpoints. |
| [Stream-backed image placements #94](https://github.com/flotilla-org/wheelhouse/issues/94) | Explore a Jackstay/terminal protocol extension across repositories. |
| [macOS Jackstay shortcuts #95](https://github.com/flotilla-org/wheelhouse/issues/95) | Host Cmd shortcuts, input ownership and relevant RAD changes. |

Metadata-driven icons and colours with local overrides, multiple compact vessel
actions, and project-level aggregate status remain presentation directions under
#90 and the sidebar design above; the initial role-icon work #79 is closed.
They must preserve stable row geometry and need not all ship in the next
presentation change. The current Jackstay implementation and limits are recorded in
[Jackstay views](design/jackstay-view.md).

The current sidebar reasoning and implementation history live in
[Andamento native reconciliation](design/andamento-native-reconciliation.md).
The [portfolio context](context/portfolio.md) is an August orientation snapshot;
the [top-down cut plan](top-down-cut-plan.md) records the earlier RAD extraction.
Neither is the current work queue. Cross-project portfolio history remains in
the separate project-map repository; Wheelhouse's active decisions and work
should be recorded here or on linked project issues.
