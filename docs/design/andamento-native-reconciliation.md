# Native sidebar follow-up to the core extraction

The next native renderer changes should consume Andamento's template semantics.
This checkout now restores the Issues display control and collects section controls
in a fixed bottom row outside scrolling content. A real ABI regression checks filtering, the resolved
checked value, and placement identity after hiding and restoring issues. Empty
sections without controls are omitted; their section state remains retained.

## Upstream work merged on 12 September 2026

[PR 90](https://github.com/flotilla-org/andamento/pull/90) merged as
`ea7a022df9b0c9320fbee868bf94357090dd3cee`.
[PR 91](https://github.com/flotilla-org/andamento/pull/91) merged as
`f8c63862e997f1e95fa80b264755e9ad270ab9ee`.
Both were revised against extraction PR 94, passed all CI checks and completed
review with no blocking findings. Wheelhouse's CI pin and local dependency checkout
now use the merged PR 91 revision. The native renderer draws the resolved first
label field, retaining the full node label for tooltips and inspection. The shipped
template still defaults to full labels; compact tiers remain an explicit choice.
The user's existing running window was left unchanged.

PR 90 resolves full/medium/short tiers in shared presentation. Explicit loop
tiers override placement-scoped variables. Short falls back to medium, then
full. Main C fields receive the selected label; node labels, facts and detail
fields retain the full text. Selection does not depend on width. Snapshot-based
terminal rendering uses ordinary end-clipping, while the compatibility path
retains its older middle elision. Native pixel measurement remains in Wheelhouse.

PR 91 exposes a typed loop invocation key in shared presentation and an opaque
snapshot-owned key through `andamento_snapshot_node_loop_key`. The additive C
accessor leaves ABI 2's node struct unchanged. Keys distinguish the region,
parent appearance and loop binding; duplicate sibling bindings are rejected.
Main content retains declared empty fields so sibling columns can align. The
terminal implementation preserves each item's hit target through inline wrapping,
including nested children under different wrapped sibling rows.

The remaining ANSI clipping concern is tracked in
[Andamento #97](https://github.com/flotilla-org/andamento/issues/97). The current
strip consumer re-renders its segments and does not use the affected run text.
Required-column overflow policy remains a documented design question.

## Concrete acceptance examples

- A convoy with one vessel can show both on one row. The vessel remains directly
  activatable, and its current-workspace state remains visible. Do not restore
  the old grouping collapse that made a vessel inaccessible.
- A convoy gaining a second vessel expands its presentation without losing the
  first vessel's identity, workspace binding or action.
- Governor placement follows producer role and project relationship facts.
  Its action may appear beside the project overview action without moving the
  governor terminal into the project overview workspace.
- Two inline loops under one project remain independently configurable even
  when both contain the same entity kind.
- A short name retains a full hover label; duplicate short labels must remain
  distinguishable through context. PR 90 explicitly leaves collision policy as
  a design question, so compact labels should not silently become the default.

## Native presentation experiment after those seams are available

Try a subtle rounded project background with a stronger project header and
lighter convoy separators. Section headings sit outside those containers and
use quieter typography. Compare an expanded convoy with a single-vessel combined
row using real catalog names, selected-workspace highlighting and narrow widths.
Do not add extra workspace tabs or make visual grouping dictate ownership.

Template detail fields can populate hover cards before terminal previews exist.
A terminal preview needs a host workspace/view binding and should only render
an existing surface. Hovering a latent entity should show metadata without
materializing a workspace. This can later be controlled by a display option.
