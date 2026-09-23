# Wheelhouse roadmap

Current as of 23 September 2026. This page is the entry point for Wheelhouse's
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

## Feature order

1. **Selected-workspace breadcrumb:** [Wheelhouse #47](https://github.com/flotilla-org/wheelhouse/issues/47).
   Show the current placement path in the toolbar, with a local-workspace
   fallback. Reuse the identity and occurrence preference used by Reveal.
2. **Standing-role and project semantics:**
   [Flotilla #1908](https://github.com/flotilla-org/flotilla/issues/1908)
   publishes declared project-role relationships and current attempts;
   [Wheelhouse #48](https://github.com/flotilla-org/wheelhouse/issues/48)
   consumes the shared Andamento interpretation for project-row actions and
   status. Role icons and ordering are presentation choices. Do not identify a
   governor from a convoy or vessel name.
3. **Complete project repository membership:**
   [Flotilla #1897](https://github.com/flotilla-org/flotilla/issues/1897) is
   ready for implementation. It must represent shared repositories, subpaths,
   removal, and known-empty versus unavailable membership. Wheelhouse can then
   show the complete set in project hover details. Until then, hover details
   only report facts the producer actually supplied.

The breadcrumb can proceed independently of the Flotilla changes. The standing
role consumer follows the producer contract and any shared Andamento template
work. The three items above are the current feature priorities, not a promise
to merge unfinished cross-repo changes in that order.

## Reliability work alongside features

[Preview isolation #11](https://github.com/flotilla-org/wheelhouse/issues/11)
and [terminal dirtiness #9](https://github.com/flotilla-org/wheelhouse/issues/9)
remain open despite narrower fixes. Recheck them before expanding preview use.
[Sidebar context scans #29](https://github.com/flotilla-org/wheelhouse/issues/29)
are a performance follow-up. [Daily-driver Flotilla binary selection #30](https://github.com/flotilla-org/wheelhouse/issues/30)
still defaults to a sibling development build. Other open defects and CI work
remain on the [Wheelhouse issue tracker](https://github.com/flotilla-org/wheelhouse/issues).

## Later decisions

[Remote Cleat attach #21](https://github.com/flotilla-org/wheelhouse/issues/21)
needs a transport decision before Wheelhouse can render remote sessions directly
without a second terminal state. Richer preview cards, convoy overview
workspaces, dynamic vessel layouts, Jackstay source discovery, and Kitty image
stream placements are later work. The first Jackstay view and its current limits
are recorded in [Jackstay views](design/jackstay-view.md).

The current sidebar reasoning and implementation history live in
[Andamento native reconciliation](design/andamento-native-reconciliation.md).
The [portfolio context](context/portfolio.md) is an August orientation snapshot;
the [top-down cut plan](top-down-cut-plan.md) records the earlier RAD extraction.
Neither is the current work queue. Cross-project portfolio history remains in
`~/dev/project-map`; Wheelhouse's active decisions and work should be recorded
here or on linked project issues.
