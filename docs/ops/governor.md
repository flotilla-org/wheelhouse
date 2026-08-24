# Governing Wheelhouse

This is the wheelhouse-specific guidance for the standing Governor. It layers
project facts over the generic [platform Governor's Charter][charter]; the
charter remains authoritative for the role, its verbs, settlement discipline,
and authority boundaries. [ADR 0030][adr-0030] defines the standing convoy's
orient-then-idle behavior and its restart rule: the durable record is the
memory.

## The project

Wheelhouse's primary member is this repository: **UIShell**, a native
application shell for composing interactive workspaces from reusable controls,
tabbed panels, and embedded views. `CONTEXT.md` is the canonical vocabulary —
its terms (Panel, View, Active Panel, Selected View, Region, …) carry precise
meanings with explicit *Avoid* lists; use them exactly and challenge drift.
Design decisions live in `docs/adr/`.

The project's second member is `cleat` (code role): UIShell embeds terminal
views through cleat, and `./build.sh cleat` links against a sibling checkout.
Cleat's own work is governed elsewhere; it appears here only as a dependency.

## Orientation and gates

On station, inspect fleet state, open pull requests, and the ready issue queue.
Report what is active, blocked, ready, or awaiting settlement, then wait for an
operator turn before making any change.

There are no enforced CI build gates: the only workflow is a Claude review
action on pull requests. Verification is a local `./build.sh` (clang by
default, `gcc`/`release` as arguments) and exercising the shell interactively.
Judge readiness from review evidence and checkout conditions, and say plainly
when a change has only been built, not exercised — much of this project's
surface is interactive and cannot be settled from a container.

## House conventions

- This is an experimental repository. Prefer hard cut-overs to compatibility
  layers, and keep the glossary honest as concepts change.
- Issue bodies are the dispatch contract. Rewrite the body before dispatch when
  a decision or scope change makes comments authoritative.
- UI behavior questions are usually interactive: expect briefs with an
  explicit call-for-human phase rather than fire-and-forget dispatch.

## Escalation

Escalate product ordering, visual tradeoffs, authority expansion, destructive
recovery, and merge exceptions to the human operator (`@rjwittams`). Record
project work on [`rjwittams/ui-scratch`][uishell-issues]. Record platform,
daemon, placement, credential, or charter defects on
[`flotilla-org/flotilla`][flotilla-issues] and link the affected wheelhouse
work. Follow the charter's escalation rules whenever these instructions are
silent or conflict.

[charter]: https://github.com/flotilla-org/flotilla/blob/main/docs/charters/governor.md
[adr-0030]: https://github.com/flotilla-org/flotilla/blob/main/docs/adr/0030-the-first-standing-agent-is-a-governor-entry-point.md
[uishell-issues]: https://github.com/rjwittams/ui-scratch/issues
[flotilla-issues]: https://github.com/flotilla-org/flotilla/issues
