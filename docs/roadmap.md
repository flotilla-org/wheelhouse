# Wheelhouse roadmap

Current as of 27 September 2026. This page is the entry point for Wheelhouse's
direction and work order. [Map issue #17](https://github.com/flotilla-org/wheelhouse/issues/17)
holds the broader destination and decisions; the linked issues hold acceptance
criteria. The design and validation notes below explain the current implementation.

## Current shape

Wheelhouse is a native workspace application built from tabbed panels and
embedded views. Cleat supplies terminal sessions and rendering data. Jackstay
supplies video and optional cooperative input to a panel. The workspace sidebar
uses Andamento's shared core and templates. It can run with local workspaces or
receive facts through HTTP over a Unix socket from the Python git producer and
`flotilla pm connect`. Flotilla is the first live fleet producer, not a
dependency of the sidebar core. Wheelhouse owns workspace activation and layout.

The project tree is now the only workspace selector. It covers local and
provider-backed workspaces, supports Reveal and Close from the toolbar, and shows
existing workspace previews on hover. A selected vessel can appear inline on a
convoy row; project rows have room for several workspace actions. These are
presentation choices over stable entity and workspace identities.

## Landed since the previous roadmap

The toolbar breadcrumb (#47), standing project-role actions (#48), and complete
project repository membership (Flotilla #1897) have landed. Managed workspaces
now reconcile one primary terminal against a stable opening intent; see
[managed primary content](design/managed-primary-content.md). The exited-session
CPU fix is in the local build through Wheelhouse PR #78 and Cleat #245.

## Current work order

This is the maintenance order, not a claim that every investigation is ready to
implement. Later design items below are deferred, not blockers for current fixes.

1. **Finish terminal reliability follow-through.** [Cursor flicker #75](https://github.com/flotilla-org/wheelhouse/issues/75)
   needs the merged Cleat synchronized-output fix adopted and verified in the
   live attachment path. [Stopped-terminal lifecycle #88](https://github.com/flotilla-org/wheelhouse/issues/88)
   is investigated in [Stopped and disconnected terminal lifecycles](design/stopped-and-disconnected-terminals.md).
   The note defines retained content, resource release, and exited versus
   disconnected behavior beyond the completed busy-loop fix; shared API work
   is tracked in [Cleat #277](https://github.com/flotilla-org/cleat/issues/277).
   [Cleat admission adoption #97](https://github.com/flotilla-org/wheelhouse/issues/97)
   bumps the Cleat pin past output-cycle admission (Cleat #268), which is a
   restart boundary for daemons, Wheelhouse and containing sessions together.
2. **Make ordinary terminal interaction work through the current attach path.**
   [Scrolling override #70](https://github.com/flotilla-org/wheelhouse/issues/70),
   [OSC 52 clipboard #71](https://github.com/flotilla-org/wheelhouse/issues/71),
   [selection lifetime #72](https://github.com/flotilla-org/wheelhouse/issues/72),
   [rectangular selection #73](https://github.com/flotilla-org/wheelhouse/issues/73),
   and [hyperlinks #77](https://github.com/flotilla-org/wheelhouse/issues/77).
   These should not wait for native remote attachment.
   The [selection, clipboard, and Edit menu review](design/terminal-clipboard.md)
   records the current gaps and delivery order. Rectangular selection #73 has
   landed. Implementation briefs cover selection lifetime #72, Shift scrolling
   #70, [wrap-aware copying #151](https://github.com/flotilla-org/wheelhouse/issues/151),
   and [Edit menu #152](https://github.com/flotilla-org/wheelhouse/issues/152).
   OSC 52 #71 follows [Cleat #240](https://github.com/flotilla-org/cleat/issues/240);
   [tracked history selection](https://github.com/flotilla-org/cleat/issues/300)
   remains deferred design work.
3. **Continue sidebar presentation and explanations.**
   [Role action icons #79](https://github.com/flotilla-org/wheelhouse/issues/79),
   [compact display controls #80](https://github.com/flotilla-org/wheelhouse/issues/80),
   and [unavailable-attachment explanations #69](https://github.com/flotilla-org/wheelhouse/issues/69).
   Keep shared interpretation in Andamento and native geometry in Wheelhouse.

The [Windows map #53](https://github.com/flotilla-org/wheelhouse/issues/53)
tracks that work separately, including remaining packaging, emoji and input
issues. [Terminal hosting transfer #81](https://github.com/flotilla-org/wheelhouse/issues/81)
follows Cleat's shared session-handle work; it does not replace the stopped-terminal
lifecycle investigation.

## Reliability work alongside features

[Preview isolation #11](https://github.com/flotilla-org/wheelhouse/issues/11)
and [terminal dirtiness #9](https://github.com/flotilla-org/wheelhouse/issues/9)
remain open despite narrower fixes. Recheck them before expanding preview use.
[Sidebar context scans #29](https://github.com/flotilla-org/wheelhouse/issues/29)
are a performance follow-up. [Daily-driver Flotilla binary selection #30](https://github.com/flotilla-org/wheelhouse/issues/30)
tracks the default development-binary selection, even though the local launcher
overrides it. [Window/tab geometry #91](https://github.com/flotilla-org/wheelhouse/issues/91)
requires fresh screenshots before classifying the older observations as current
bugs; [GPU regression coverage #41](https://github.com/flotilla-org/wheelhouse/issues/41)
tracks thin borders and popup blur.

Other open defects and CI work remain on the
[Wheelhouse issue tracker](https://github.com/flotilla-org/wheelhouse/issues).

## Upstream ownership

- [Cleat #240](https://github.com/flotilla-org/cleat/issues/240) owns OSC 52 effect
  transport; Wheelhouse #71 owns desktop integration.
- [Cleat #241](https://github.com/flotilla-org/cleat/issues/241) owns nested
  attachment scrollback semantics; Wheelhouse #70 owns the native override.
- [Cleat #242](https://github.com/flotilla-org/cleat/issues/242) is closed with
  the synchronized-output fix; Wheelhouse #75 remains open for adoption and
  live confirmation.
- [Flotilla #1961](https://github.com/flotilla-org/flotilla/issues/1961) owns
  readiness/blocker publication; Wheelhouse #69 consumes it through Andamento.
- Flotilla [#1975](https://github.com/flotilla-org/flotilla/issues/1975) and
  [#1976](https://github.com/flotilla-org/flotilla/issues/1976), terminal identity
  and crew Ghostty terminfo, are closed. Deployment into an existing crew is
  separate from merged source; do not diagnose remaining styling from TERM alone.

## Later decisions

| Investigation | Scope |
| --- | --- |
| [Remote Cleat attach #21](https://github.com/flotilla-org/wheelhouse/issues/21) | Transport and direct rendering without duplicate VT state. |
| [Preview policy and cards #89](https://github.com/flotilla-org/wheelhouse/issues/89) | Hydration, cached/shared previews, interactive cards and pinning. |
| [Workspace composition #90](https://github.com/flotilla-org/wheelhouse/issues/90) | Convoy overviews, dynamic managed content, contextual new views, naming and cross-project compositions. |
| [Accessibility #92](https://github.com/flotilla-org/wheelhouse/issues/92) | Assess native accessibility and a first integration slice. |
| [Jackstay discovery #93](https://github.com/flotilla-org/wheelhouse/issues/93) | Source selection while preserving explicit endpoints. |
| [Stream-backed image placements #94](https://github.com/flotilla-org/wheelhouse/issues/94) | Explore a Jackstay/terminal protocol extension across repositories. |
| [macOS Jackstay shortcuts #95](https://github.com/flotilla-org/wheelhouse/issues/95) | Host Cmd shortcuts, input ownership and relevant RAD changes. |

Metadata-driven icons and colours with local overrides, multiple compact vessel
actions, and project-level aggregate status remain presentation directions under
#79 and #90. They must preserve stable row geometry and need not all ship in the
first icon change. The current Jackstay implementation and limits are recorded in
[Jackstay views](design/jackstay-view.md).

The current sidebar reasoning and implementation history live in
[Andamento native reconciliation](design/andamento-native-reconciliation.md).
The [portfolio context](context/portfolio.md) is an August orientation snapshot;
the [top-down cut plan](top-down-cut-plan.md) records the earlier RAD extraction.
Neither is the current work queue. Cross-project portfolio history remains in
the separate project-map repository; Wheelhouse's active decisions and work
should be recorded here or on linked project issues.
