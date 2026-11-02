# Wheelhouse roadmap

Current as of 2 November 2026. This page is the entry point for Wheelhouse's
direction and work order. [Map issue #17](https://github.com/flotilla-org/wheelhouse/issues/17)
holds the broader destination and decisions; the linked issues hold acceptance
criteria. The design and validation notes below explain the current implementation.

## Current shape

Wheelhouse is a native workspace application built from tabbed panels and
embedded views, on macOS, Linux and Windows. Cleat supplies terminal sessions and
rendering data. Jackstay supplies video and optional cooperative input to a panel.
The core (`wheelhouse-core`, formerly Andamento) now lives in this repo (#440) and
owns the Dashboard's and each Workspace's logical state; the
[platform direction](design/platform-direction.md) puts web and TUI frontends over
it beside the native app. The workspace sidebar can run with local workspaces or
receive facts through HTTP over a Unix socket (a named pipe on Windows) from the
native git watcher and from `flotilla pm connect`, one connector per Dashboard
subscription. Flotilla is the first live fleet producer, not a dependency of the
core. Wheelhouse owns workspace activation and layout.

A subject's workspace (a vessel, convoy or project) holds its Views in Slots, drawn
through a Slot Host and arranged by a Workspace Layout, with a Tool Strip at the
edge of the Workspace Region. The overview is one scene that every switch of
workspace flies through. Markdown files open in the Rich Text View, and tables,
canned or described, in the Table View. Every instance serves a read-only
Automation Endpoint. The README's
[features by platform](../README.md#features-by-platform) table says where each of
these works.

## Landed since the previous roadmap

Merged from 6 October to 1 November 2026:

- **State model:** the migration below landed in the State model A–E PRs (#319,
  #328, #337, #342, #354), then panels by arrangement ID, Views by View Address and
  the arrangement as the layout (#368, #370, #371). The Floating Panel machinery is
  gone (#387).
- **The core in this repo:** Andamento merged in as `wheelhouse-core` (#454). The
  sidebar follows the core's change stream in native and in a browser page (#478,
  #481, #494; [design](design/change-stream.md)), and arrangement edits and docking
  rules moved into the core (#518), as steps 0 and 1 of
  [#441](https://github.com/flotilla-org/wheelhouse/issues/441).
- **Sidebar:** local sections, groups and pins as data, entity ghosts, drag to
  reorder and regroup, and redesigned headers (#235–#284); workspace close and
  creation moved into the sidebar (#235) and hosting status into tab settings
  (#231). Only the rows in view are built (#501).
- **Subject workspaces** ([design](design/subject-workspaces.md), #413): Slot Host
  (#418), opening without a recipe (#419), Workspace Layouts (#416, #442) and the
  Tool Strip (#122, #446, #503).
- **Overview** ([design](design/overview-scene.md), #450): one scene with camera
  flights (#523, #524, #525, #528), the grid's layout rule (#550), changes of
  layout as movement (#555), and drags and zoom by hand (#602, #604).
- **Terminals:** remote crew terminals as direct Cleat views over ssh (#104, #444,
  #471, #482; [design](design/remote-terminals.md)) and one Cleat attachment per
  session (#417, #517). #104 stays open for live acceptance on the owner's machine.
  Cleat was bumped to keep an exited session's screen (#549).
- **Web views:** a Handler launches Luchs and the View shows its page (#393;
  [design](design/web-views.md)). Pages render on macOS only.
- **Rich Text** ([map #537](https://github.com/flotilla-org/wheelhouse/issues/537)):
  the native text stack (`wheelhouse-text` over parley and fontique, #567, #580;
  the glyph cache, #587, #588), the core's model (#558), the Rich Text View for
  markdown files (#561, #598, #599), and conversation events streamed into it
  through the C ABI (#614, #625–#627, with an interim Claude Code translator, #620).
- **Table View:** binnacle tables over canned data (#508), described tables by
  their link (#529, #530), keyboard navigation (#551) and scroll bars (#585).
- **Automation Endpoint** ([protocol](protocol/automation-endpoint.md), map #343,
  [ADR 0014](adr/0014-automation-endpoint-invokes-named-commands.md)): every
  instance has its own endpoint (#565) serving the Semantic Tree, Caller Context
  and an event stream (#569, #618).
- **Linux:** idle CPU, NVIDIA and glyph fixes (#606), live resize and one title
  bar (#608), paste from other apps, key releases and wheel units (#610).
  [ADR 0015](adr/0015-window-manager-and-render-backends-are-added-beside-each-other.md)
  (proposed) adds backends beside each other, starting with GL on Linux through EGL
  (#642).
- **Performance and CI:** frames are built only when presented and Metal
  presentation is paced (#402, #403, #408, #510); CI pins and UI diagnostic lists
  live in `ci/` (#511, #515); macOS jobs run on macos-15 (#552).

## Current work order

The previous order (6 October) is done except for re-measuring hover-card relation
caching after the snapshot work (#179), the overview brainstorm (#190), nested
Controlled Splits (#164) and notices (#110, #108). No new order has been set since;
the open threads below are grouped by area, not ranked.

- **Windows gaps** from a run on Beaufort on 1 November, where the Windows native
  CI checks all pass locally: Cleat provider ABI 12 (#644), CRLF markdown in Rich
  Text (#645), diagnostics losing stdout on exit and the table diagnostic in Windows
  CI (#646), and the git watcher over a named pipe (#467). The
  [Windows map #53](https://github.com/flotilla-org/wheelhouse/issues/53) tracks the
  wider Windows work.
- **The core's boundary:** the remaining [#441](https://github.com/flotilla-org/wheelhouse/issues/441)
  steps, starting with workspace lifecycle and homing (#631).
- **Rich Text:** conversations from a file (#616), then following a growing one
  (#617); code Blocks embed the code view (#622), after the highlighter decision
  (#621); inline images (#609).
- **Tables:** parity with binnacle's web host (#535), viewing Flotilla's tables
  (#577), and the rest of the canned-data pilot (#506).
- **Automation Endpoint:** commands and intents (#643), the Placement Tree (#564),
  and strings phase 1 (#557).
- **Subject workspaces:** the mission (#401) and what it needs from Flotilla (#414).

[Cleat admission adoption #97](https://github.com/flotilla-org/wheelhouse/issues/97)
remains open for rollout and acceptance evidence. Cleat #277 owns the remaining
stopped-document lifecycle API; the
[stopped/disconnected lifecycle note](design/stopped-and-disconnected-terminals.md)
records the design.

**Blocked upstream:** unavailable-attachment explanations (#69) wait on
Flotilla #1961. Linux clipboard targets/PRIMARY/large transfers remain tracked in
[#150](https://github.com/flotilla-org/wheelhouse/issues/150).

## Reliability work alongside features

[Preview isolation #11](https://github.com/flotilla-org/wheelhouse/issues/11)
and [terminal dirtiness #9](https://github.com/flotilla-org/wheelhouse/issues/9)
remain open despite narrower fixes. Recheck them before expanding preview use.
[Window/tab geometry #91](https://github.com/flotilla-org/wheelhouse/issues/91)
requires fresh screenshots before classifying the older observations as current
bugs; [GPU regression coverage #41](https://github.com/flotilla-org/wheelhouse/issues/41)
tracks thin borders and popup blur. The daily driver's main thread at about 100%
CPU is [#475](https://github.com/flotilla-org/wheelhouse/issues/475).

Other open defects and CI work remain on the
[Wheelhouse issue tracker](https://github.com/flotilla-org/wheelhouse/issues).

## Upstream ownership

- [Cleat #240](https://github.com/flotilla-org/cleat/issues/240) owned OSC 52 effect
  transport (Cleat PR #304); Wheelhouse #71 delivered pin adoption and desktop
  integration across provider ABI 11 and packet protocol 12. Cleat's provider ABI
  12 (cleat#407) is not adopted yet (#644).
- [Cleat #241](https://github.com/flotilla-org/cleat/issues/241) owns nested
  attachment scrollback semantics; Wheelhouse #70 delivered the outer native
  override. That does not expose all inner Cleat history. Direct remote views
  (#104) avoid the nesting for subjects with a direct endpoint.
- [Cleat #285](https://github.com/flotilla-org/cleat/issues/285) supplied
  `cleat proxy` and provider transports for #104, and
  [Flotilla #2106](https://github.com/flotilla-org/flotilla/issues/2106) direct
  endpoint recipes; both are closed.
- [Flotilla #1961](https://github.com/flotilla-org/flotilla/issues/1961) owns
  readiness/blocker publication; Wheelhouse #69 consumes it through the core.
- [Flotilla #4206](https://github.com/flotilla-org/flotilla/issues/4206) owns the
  live conversation adapters that feed Rich Text (#548).

## State model migration

Decided on [Map: a target state model for Wheelhouse](https://github.com/flotilla-org/wheelhouse/issues/290)
(10 October 2026): the core owns logical state (the Dashboard, workspaces, slots,
and arrangements committed as whole documents) and saves it as records in a
Dashboard directory; RAD's config tree keeps only Presentation State. See ADRs
[0012](adr/0012-andamento-owns-logical-state.md) and
[0013](adr/0013-workspace-overlay-is-an-addressed-edit-set.md) and the glossary in
`CONTEXT.md`. Every Wheelhouse step landed on 12 October, in five PRs:

| Step | Wheelhouse | Landed in | Core / Flotilla |
| --- | --- | --- | --- |
| 0. Behaviour harness with golden logical-state snapshots | [#314](https://github.com/flotilla-org/wheelhouse/issues/314) | A (#319) | |
| 1. Host-supplied 128-bit Workspace IDs | [#306](https://github.com/flotilla-org/wheelhouse/issues/306) | B (#328) | [andamento#141](https://github.com/flotilla-org/andamento/issues/141) |
| 2. Save visible workspace and focus; autosave only on change | [#315](https://github.com/flotilla-org/wheelhouse/issues/315) | B (#328) | |
| 3. The core owns sidebar state as records; delete the replay | [#307](https://github.com/flotilla-org/wheelhouse/issues/307) | C (#337) | [andamento#142](https://github.com/flotilla-org/andamento/issues/142) |
| 4. Dashboard directory and `--dashboard` | [#316](https://github.com/flotilla-org/wheelhouse/issues/316) | C (#337) | |
| 5. Subscription providers, one ingress endpoint each; stale facts | [#308](https://github.com/flotilla-org/wheelhouse/issues/308) | C (#337) | [andamento#143](https://github.com/flotilla-org/andamento/issues/143) |
| 6. Multi-slot Suggested Layout fact schema | | | [andamento#147](https://github.com/flotilla-org/andamento/issues/147) (closed); [flotilla#3012](https://github.com/flotilla-org/flotilla/issues/3012) (open) |
| 7. Typed arrangement module (7a), then slots and `set_arrangement` (7b) | [#311](https://github.com/flotilla-org/wheelhouse/issues/311), [#309](https://github.com/flotilla-org/wheelhouse/issues/309) | 7a: B, C; 7b: D (#342), E (#354) | [andamento#144](https://github.com/flotilla-org/andamento/issues/144) |
| 8. Sidebar arrangement as a Dashboard document | [#317](https://github.com/flotilla-org/wheelhouse/issues/317) | D (#342) | [andamento#148](https://github.com/flotilla-org/andamento/issues/148) |
| 9. Renderer registry and `WH_ViewContext` | [#310](https://github.com/flotilla-org/wheelhouse/issues/310) | D (#342) | |
| 10. Workspace Overlay | | E (#354) | [andamento#145](https://github.com/flotilla-org/andamento/issues/145) (closed) |

Moving the remaining logical-state rules into the core is
[#441](https://github.com/flotilla-org/wheelhouse/issues/441). Still deferred: one
core per process (with the window effort); typed Presentation State accessors and an
explicit settings cascade ([#312](https://github.com/flotilla-org/wheelhouse/issues/312));
retiring the evaluator ([#313](https://github.com/flotilla-org/wheelhouse/issues/313));
the provider-agnostic facts stream ([flotilla#3011](https://github.com/flotilla-org/flotilla/issues/3011),
[andamento#146](https://github.com/flotilla-org/andamento/issues/146)). Build speed
work: [#303](https://github.com/flotilla-org/wheelhouse/issues/303) and
[#304](https://github.com/flotilla-org/wheelhouse/issues/304) remain open; #302 landed.

## Later decisions

| Investigation | Scope |
| --- | --- |
| [Preview policy and cards #89](https://github.com/flotilla-org/wheelhouse/issues/89) | Hydration, cached/shared previews, interactive cards and pinning. |
| [Workspace composition #90](https://github.com/flotilla-org/wheelhouse/issues/90) | Convoy overviews, dynamic managed content, contextual new views, naming and cross-project compositions. Subject workspaces (#401) build its first stage. |
| [Overview brainstorm #190](https://github.com/flotilla-org/wheelhouse/issues/190) | Structure, subject titles and a model shared with the sidebar; #450 answered part of it. |
| [Jackstay discovery #93](https://github.com/flotilla-org/wheelhouse/issues/93) | Source selection while preserving explicit endpoints. |
| [Stream-backed image placements #94](https://github.com/flotilla-org/wheelhouse/issues/94) | Explore a Jackstay/terminal protocol extension across repositories. |
| [macOS Jackstay shortcuts #95](https://github.com/flotilla-org/wheelhouse/issues/95) | Host Cmd shortcuts, input ownership and relevant RAD changes. |

Accessibility (#92) was planned on map #343: AccessKit owned by the toolkit.
Metadata-driven icons and colours with local overrides, multiple compact vessel
actions, and project-level aggregate status remain presentation directions under
#90 and the [sidebar design](design/sidebar-cards-and-docking.md). They must
preserve stable row geometry. The current Jackstay implementation and limits are
recorded in [Jackstay views](design/jackstay-view.md).

The current sidebar reasoning and implementation history live in
[Core native reconciliation](design/andamento-native-reconciliation.md).
The [portfolio context](context/portfolio.md) is an August orientation snapshot;
the [top-down cut plan](top-down-cut-plan.md) records the earlier RAD extraction.
Neither is the current work queue. Cross-project portfolio history remains in
the separate project-map repository; Wheelhouse's active decisions and work
should be recorded here or on linked project issues.
