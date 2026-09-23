# Portfolio context for the wheelhouse governor

This is an orientation snapshot from 26 August 2026, not the current work queue.
For present Wheelhouse state and priorities, start with the
[roadmap](../roadmap.md). Check linked project tickets for later changes.

Curated orientation, written 2026-08-26 by the operator side. Your vessel can
read the wheelhouse and cleat checkouts and their GitHub repos; everything
else in the portfolio is *described here* because you cannot currently reach
it (your credentials are project-scoped by design; the lab planning repo and
other projects' repos are not visible to you). Treat this document as durable
context, not gospel: where it conflicts with something you can read directly,
prefer the primary source and flag the conflict.

## The portfolio, one paragraph each

- **flotilla** (github.com/flotilla-org/flotilla) — the fleet system that runs
  you: daemon mesh, control plane, convoys/crews, TUI. Mid-transition from a
  provider-observer architecture ("Plane A") to a k8s-isomorphic
  resource/controller control plane ("Plane B"); the transition roadmap lives
  in its `docs/roadmap.md`, glossary in `CONTEXT.md`, decisions in
  `docs/adr/` (0001–0037+). You experience flotilla as: ensures keep you
  running, briefs arrive as turns, settlement claims and reviews are how work
  lands.
- **cleat** (your second member; you can read it) — terminal I/O engine: PTY
  management, VT engine, session persistence, recordings. Ships a provider
  C API (ABI v8) and a multiplexed daemon packet protocol. Wheelhouse embeds
  it for terminal rendering; the intended end state is wheelhouse consuming
  cleat *render output* natively (one VT engine, not two), locally first and
  remotely later.
- **wheelhouse** (you) — native UI shell: C codebase, uishell + window
  manager, builds via `./build.sh wheelhouse` with sibling Cleat, Andamento,
  and Jackstay checkouts (`bundle` packages the macOS app). It attaches to
  Cleat sessions, renders fleet state through the Andamento sidebar, and can
  host Jackstay video/input views. In-app review artifacts remain future work.
- **andamento** — a terminal-first project/agent dashboard used daily inside
  a **zellij fork**: andamento runs as a zellij plugin surface (the fork
  carries the patches zellij needs for it). It consumes flotilla's control
  plane through the presentation-manager connector ("pm connect"): flotilla's
  daemon serves it the presentable tree (projects, convoys, attention).
  Governor exists for it like you exist for wheelhouse.
- **ghostty fork** (rjwittams/ghostty) — a patch set (not a divergent fork)
  carrying what cleat's ghostty-vt feature needs; per-patch branches
  integrated into a force-pushed integration branch. Its project never opens
  PRs anywhere and never touches upstream ghostty-org. It is the forcing
  function for in-crew review (ADR 0036 in the flotilla repo).
- **The lab** — a Forgejo hub (forgejo.lab.flotilla.work) holds trackers and
  mirrors; a planning repo ("project-map") holds manifests, briefs, and the
  operator's cross-project planning. You cannot read it; assume referenced
  decisions are recorded there or in flotilla ADRs.

## The transition, as it affects planning

Flotilla is finishing the Plane-A→Plane-B move: provider config and identity
are moving into replicated resources (Repository/Project), review is moving
from PR-threads to claim-evidence bundles in object storage (ADR 0036, with
an artifacts store now live in the lab), and the release pipeline is being
rebuilt around a format contract, a rehearsal gate, and per-project artifact
composition (ADR 0037 and its slices). Standing governors (you, andamento's,
soon ghostty's) are the ADR 0030 dogfood: durable-record memory, one
orientation sweep per start, operator-gated mutations.

## Working conventions you should follow

- Wayfinder maps and their tickets live on your tracker (the wheelhouse
  GitHub repo's issues). Decisions get recorded on tickets; the map indexes
  them.
- Skills staged in your vessel include wayfinder, grilling, domain-modeling,
  to-issues, research, pr-shepherd (self-contained). Helper scripts are
  relative to each skill's base directory.
- When you need context this document lacks, say precisely what and why —
  the operator can widen access or extend this document; do not guess.
