# A Workspace Overlay Is an Addressed Edit Set Against a Cached Baseline

**Status:** accepted, not yet implemented. Decided in
[Define Workspace Overlay semantics against a Suggested Layout](https://github.com/flotilla-org/wheelhouse/issues/298),
choosing from the models in
[Survey prior art for baseline-plus-user-overlay workspace definitions](https://github.com/flotilla-org/wheelhouse/issues/293).

## Context

A provider such as flotilla supplies a **Suggested Layout** for a workspace,
and the user then edits it. Today the suggested layout is written into the
config tree once and the edits overwrite it, so the two cannot be told apart
and a changed suggestion cannot be applied. The same holds for the Dashboard
over its template. We want the provider's changes to keep arriving, the user's
edits to survive them, and the edits to be proposable back to the provider
later.

## Decision

A **Workspace Overlay** is an **addressed edit set**: edits keyed by slot key
and panel ID, stored with the version of the Suggested Layout they were made
against and a **cached copy of that baseline**. It is a normalised set, not a
history; history comes from version control of the saved files.

- **Per-slot edits:** add a slot, remove a baseline slot (kept as a
  tombstone), override a slot's content, change its rebind policy, rename the
  workspace, change its mood.
- **Arrangement:** the first time the user rearranges anything, the overlay
  owns the whole arrangement (panels, tab order, splits, weights). Baseline
  slots that appear later are placed by a default rule.
- **Detached:** a slot whose content is overridden stops following the
  provider. Reattaching drops the override. This is a flag beside the slot's
  resolution state, not a sixth state.
- **When the baseline changes,** nothing is dropped silently:

  | Change | Result |
  |---|---|
  | New baseline slot | Appears, placed by the default rule. |
  | Untouched slot removed | Goes away; its live instance follows the slot's rebind policy. |
  | Overridden slot removed | Flagged, kept as the user's own slot. |
  | Untouched slot's content changes | Follows the change through an Updating rebind. |
  | Overridden slot's content changes | The override wins; the slot is flagged. |

- A workspace with no Suggested Layout uses the same model with an empty
  baseline. The Dashboard relates to its template the same way.
- An **Overlay Sync** proposal is the edit set plus the baseline version it
  applies to; accepted edits drop out once they equal the new baseline.

## Alternatives rejected

- **Fork and own** (tmuxinator, zellij layouts): baseline updates stop arriving.
- **Baseline wins** (Grafana provisioning): silently overwrites user edits.
- **Replayed edit log** (git rebase): adds history the files' version control
  already provides.
- **Computed three-way diff** (Copier): needs the provider to reproduce old
  baselines; the cached copy removes that dependency.
- **CRDT:** merges never fail, which hides baseline conflicts instead of
  surfacing them. It may still sit underneath Overlay Sync later.
