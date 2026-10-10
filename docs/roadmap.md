# Wheelhouse roadmap

Current as of 6 October 2026. This page is the entry point for Wheelhouse's
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

Terminal interaction: the Edit menu and unified clipboard commands (#152) and
OSC 52 delivery to the desktop clipboard (#71) have landed. #71 adopted Cleat's
provider ABI 11 and packet protocol 12, so a Wheelhouse built at the current
`CLEAT_REV` attaches only to protocol-12 daemons. The daily driver and the
fleet's Cleat move across that boundary together.

Sidebar presentation, following the
[sidebar rows, cards and docking design](design/sidebar-cards-and-docking.md):

- **Rows:** convoy rows carry compact PR and issue chips (#160). Unopened
  workspace actions fold into `+N` before quiet issue chips (#175).
- **Ended subjects:** an ended subject's workspace is retained, marked ended
  (#123). While that workspace is open, its subject row stays visible even with
  Show finished off. Every open workspace has exactly one reachable sidebar
  entry, and selection highlights one row or the nearest collapsed ancestor
  (#188, #189). Restoration no longer re-inspects unavailable saved workspaces
  on every patch (#204).
- **Hover cards:** peek and engaged hover cards (#165), structured from
  Andamento's typed detail roles (#177). Cards can be detached to float, docked
  under their source, or pinned (#166).
- **Docked sections:** sidebar sections are docked Views with compact display
  toggles in their headers (#163, #80; the history toggle is **Role history**).
  Docking validity uses one checker (#161) against measured drop geometry
  (#180). Andamento regions declare `default-host` and `order` hints, and
  Wheelhouse's saved layout stores where sections actually are (#162).
- **Performance:** a capped native benchmark and a snapshot-scoped
  context-label lookup (#182, #29). A shared section-geometry cache was declined
  on measurements.

## Current work order

This is the maintenance order, not a claim that every investigation is ready to
implement. Later design items below are deferred, not blockers for current fixes.

1. **Sidebar polish round (kiwi, with the owner):** restoring closed sections
   (#183), consistent movement affordances (#184) and chip reachability (#174),
   plus the retention policy for ended pinned cards (#202).
2. **Docking tidy (headless):** emptied-source removal in drop geometry (#196,
   which gates any View declaring a minimum width), diagnostics (#197), one
   canonical acceptance note (#198) and the memory benchmark as a scheduled CI
   check (#199).
3. **Snapshot cost:** shared evaluation and demand-driven details in
   [Andamento #96](https://github.com/flotilla-org/andamento/issues/96), then
   Wheelhouse requests details only for live card targets. The release daily
   driver measured about 70% of a core while idle, mostly in detailed snapshot
   acquisition. Re-measure hover-card relation caching (#179) afterwards.
4. **Overview and Workspace Subject (kiwi, Opus):** the overview brainstorm
   ([#190](https://github.com/flotilla-org/wheelhouse/issues/190)) shapes what a
   Workspace Subject carries for titles, grouping and colour. Then the subject
   toolbar and `+` menu ([#122](https://github.com/flotilla-org/wheelhouse/issues/122)),
   then nested Controlled Splits (#164).
5. **Notices and hosting status:** route notices by visible affordance (#110),
   with hosting failures (#108) and hosting actions in tab settings (#109).

[Cleat admission adoption #97](https://github.com/flotilla-org/wheelhouse/issues/97)
remains open for rollout and acceptance evidence. Cleat #277 owns the remaining
stopped-document lifecycle API; the
[stopped/disconnected lifecycle note](design/stopped-and-disconnected-terminals.md)
records the design.

**Blocked upstream:** unavailable-attachment explanations (#69) wait on
Flotilla #1961. A governor dropping off its project row across a roll or daemon
restart ([Andamento #105](https://github.com/flotilla-org/andamento/issues/105))
is a Flotilla projection defect; its regression fixtures are in Andamento #130.

The [Windows map #53](https://github.com/flotilla-org/wheelhouse/issues/53)
tracks that work separately, including remaining packaging, emoji and input
issues. Linux clipboard targets/PRIMARY/large transfers remain tracked in
[#150](https://github.com/flotilla-org/wheelhouse/issues/150).

## Reliability work alongside features

[Preview isolation #11](https://github.com/flotilla-org/wheelhouse/issues/11)
and [terminal dirtiness #9](https://github.com/flotilla-org/wheelhouse/issues/9)
remain open despite narrower fixes. Recheck them before expanding preview use,
including the overview work in #190.
[Window/tab geometry #91](https://github.com/flotilla-org/wheelhouse/issues/91)
requires fresh screenshots before classifying the older observations as current
bugs; [GPU regression coverage #41](https://github.com/flotilla-org/wheelhouse/issues/41)
tracks thin borders and popup blur.

Other open defects and CI work remain on the
[Wheelhouse issue tracker](https://github.com/flotilla-org/wheelhouse/issues).

## Upstream ownership

- [Cleat #240](https://github.com/flotilla-org/cleat/issues/240) owned OSC 52 effect
  transport (Cleat PR #304); Wheelhouse #71 delivered pin adoption and desktop
  integration across provider ABI 11 and packet protocol 12.
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

## State model migration

Decided on [Map: a target state model for Wheelhouse](https://github.com/flotilla-org/wheelhouse/issues/290)
(10 October 2026) and revised the same day after five adversarial reviews:
Andamento owns logical state (the Dashboard, workspaces, slots, and
arrangements committed as whole documents) and saves it as named KDL records
in a Dashboard directory; RAD's config tree keeps only Presentation State. See
ADRs [0012](adr/0012-andamento-owns-logical-state.md) and
[0013](adr/0013-workspace-overlay-is-an-addressed-edit-set.md) and the glossary
in `CONTEXT.md`. Each step keeps the daily driver usable; step 4 is the one
planned fresh start. Andamento's ABI 3 is additive; Wheelhouse names
workspaces only by Workspace ID from step 1, so it requires ABI 3.

| Step | Wheelhouse | Andamento / Flotilla |
| --- | --- | --- |
| 0. Behaviour harness with golden logical-state snapshots | [#314](https://github.com/flotilla-org/wheelhouse/issues/314) | |
| 1. Host-supplied 128-bit Workspace IDs (ABI 3) | [#306](https://github.com/flotilla-org/wheelhouse/issues/306) | [andamento#141](https://github.com/flotilla-org/andamento/issues/141) |
| 2. Save visible workspace and focus; autosave only on change | [#315](https://github.com/flotilla-org/wheelhouse/issues/315) | |
| 3. Andamento owns sidebar state as records; delete the replay | [#307](https://github.com/flotilla-org/wheelhouse/issues/307) | [andamento#142](https://github.com/flotilla-org/andamento/issues/142) |
| 4. Dashboard directory and `--dashboard` | [#316](https://github.com/flotilla-org/wheelhouse/issues/316) | |
| 5. Subscription providers, one ingress endpoint each; stale facts | [#308](https://github.com/flotilla-org/wheelhouse/issues/308) | [andamento#143](https://github.com/flotilla-org/andamento/issues/143) |
| 6. Multi-slot Suggested Layout fact schema (can start any time) | | [andamento#147](https://github.com/flotilla-org/andamento/issues/147), [flotilla#3012](https://github.com/flotilla-org/flotilla/issues/3012) |
| 7. Typed arrangement module (7a), then slots and `set_arrangement` (7b) | [#311](https://github.com/flotilla-org/wheelhouse/issues/311), [#309](https://github.com/flotilla-org/wheelhouse/issues/309) | [andamento#144](https://github.com/flotilla-org/andamento/issues/144) |
| 8. Sidebar arrangement as a Dashboard document | [#317](https://github.com/flotilla-org/wheelhouse/issues/317) | [andamento#148](https://github.com/flotilla-org/andamento/issues/148) |
| 9. Renderer registry and `WH_ViewContext` | [#310](https://github.com/flotilla-org/wheelhouse/issues/310) | |
| 10. Workspace Overlay | | [andamento#145](https://github.com/flotilla-org/andamento/issues/145) |

Deferred: one Andamento core per process (with the window effort); typed
Presentation State accessors and an explicit settings cascade ([#312](https://github.com/flotilla-org/wheelhouse/issues/312)); retiring
the evaluator ([#313](https://github.com/flotilla-org/wheelhouse/issues/313)); the provider-agnostic facts stream ([flotilla#3011](https://github.com/flotilla-org/flotilla/issues/3011),
[andamento#146](https://github.com/flotilla-org/andamento/issues/146)). Flotilla's provider identity is decided separately in
[flotilla#3010](https://github.com/flotilla-org/flotilla/issues/3010). Build speed work from the same research is independent:
[#302](https://github.com/flotilla-org/wheelhouse/issues/302), [#303](https://github.com/flotilla-org/wheelhouse/issues/303), [#304](https://github.com/flotilla-org/wheelhouse/issues/304).

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
