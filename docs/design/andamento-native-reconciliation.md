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

## Design direction and proposed follow-up, 22 September 2026

Treat this as a workspace navigation panel whose default presentation is a tree.
Do not constrain templates to one icon or one action per row. Role icons need
semantic role identifiers, text fallbacks and individually labelled hit targets.
Project identity should have a stable, overridable colour accent, independent of
workspace selection and status. Avoid recolouring the entire workspace. Multiple
inline actions must retain their own workspace bindings and selection feedback.
Project hover details should include repository membership where the producer
supplies it; missing facts must not be guessed from display names.

Before retiring the Workspaces selector, enforce coverage of every materialized
workspace. Reconcile the host inventory with visible placements after template
filtering. Workspaces without a visible placement need a fallback in this panel,
including local/ad hoc workspaces, restored bindings, and workspaces whose provider
has disconnected or removed their entity. Multiple placements may reference one
workspace; fallback membership must use stable workspace identity, not labels.
Collapsed ancestors need a discoverable route to the selected workspace. Coverage
and placement decisions belong in shared Andamento; Wheelhouse supplies inventory
and executes host actions. Workspace creation and closing also need accessible
controls before the old selector can go away.

Reuse Wheelhouse's demand-driven workspace preview surfaces for hover cards.
Metadata is available without opening a workspace; previews reference existing
materialized workspaces and never activate or create one on hover. Initially this
can be a native hover treatment. A later template preview reference should name
the bound workspace, leaving surface handles, sizing and rendering in the host.
Check preview cost before enabling many simultaneous inline previews.

The daily driver can disable the separate local git producer with `--no-git`.
This removes duplicate discovery for now; future merging needs explicit project
and repository identity, rather than matching project titles.

Suggested order: establish workspace coverage and fallback behavior; add metadata
and preview hover cards; then compare project accents, role actions and compact
row compositions using real data at narrow and wide sidebar widths. Keep fixed
horizontal slots and stable status geometry throughout.


### Workspace coverage implemented

Andamento now appends “Other workspaces” for observed workspace IDs with no
catalog placement after filtering. It removes that fallback when a normal
placement returns, and keeps duplicate names distinct. Snapshot-owned activation
focuses the exact workspace ID. The native renderer outlines collapsed ancestors
(including section headers) containing the current workspace without changing
row geometry or assigning the ancestor the child's activation target.

The old selector remains available. Preview hover cards, creation/closing controls,
and automatic scroll-to-current behavior are still separate follow-up work.
Validation: independent core/frontends and C fixture, twelve native ABI tests,
and a native Wheelhouse build passed on 22 September 2026.


### Hover cards implemented

Native hover cards now consume the independent template detail fields, omit empty
and duplicate lines, and wrap long values within a bounded width. The daily-driver
templates select project counts and supplied status, phase, host, repository,
branch and checkout-path facts. Compact placement templates preserve the separate
detail template in Andamento.

A live row requests its bound workspace's existing preview surface. The card
reserves a fixed preview area and fits the image without changing its aspect.
Latent rows do not request previews or materialize workspaces. Outside workspace
zoom, only inactive workspaces with recent preview demand are built; hovering one
row no longer asks every inactive workspace to render. Preview demand expires
using the existing short frame grace period.

Complete project repository membership is not inferred from activity. Flotilla
issue https://github.com/flotilla-org/flotilla/issues/1897 specifies the missing
producer contract, including shared repositories, subpaths and known-empty versus
unavailable membership.

Validation: independent core/frontend tests and C fixture, thirteen native sidebar
ABI tests, a native build, and live inspection of a project metadata card and a
vessel terminal preview passed on 22 September 2026. The old Workspaces selector
remains available; workspace creation/closing controls are still outstanding.

### Native project composition, 22 September 2026

The accepted browser study combines subtle project containers with inline workspace
controls. The first native implementation uses Andamento's existing `layout="inline"`
loop declaration for vessel children. Each placement retains its own key, action,
workspace binding, selection and hover details. Wheelhouse measures the action
budget and puts excess actions in a `+N` menu; the menu indicates a selected member.
An inline leaf stays on its parent's row even if that parent has retained collapse
state. Nodes with children or controls retain ordinary tree rows so their content
remains reachable. Placement membership, filtering and workspace coverage remain
in Andamento; the native index only arranges the returned nodes for drawing.

Top-level projects receive a subtle rounded container and an identity-derived
accent. The palette is a local default, independent of sort order, labels and
workspace selection. It does not recolour terminal contents. A project overview
button has a fixed slot even when no recipe is available or an opening is pending.
Vessel controls currently use template-resolved text labels, with full labels in
their hover cards. Open/pending/status marks reserve space; selecting a workspace
does not insert another column. Diagnostics occupy one fixed bottom strip.

Future presentation resolution should accept producer icon/colour suggestions,
then apply local overrides ahead of defaults. Those visual choices must remain
separate from entity role and grouping identity. No metadata format is introduced
by this change. Standing-role actions on project headers still need explicit
producer relationships; the renderer does not guess a governor from its name.
Creation/closing and reveal-current controls remain prerequisites for retiring the
old workspace selector. Other workspaces remains the coverage fallback.

Validation: the native macOS build and fourteen typed ABI tests pass, including
independent inline sibling activation and focus after a parent collapse. A separate
native fixture window was used to open a workspace from overflow and check project
spacing, status slots and selected-row treatment. This does not replace the live
daily-driver configuration or validate future role metadata.

The native spacing follow-up insets selection outlines from project containers,
separates adjacent action borders, and insets action outlines within selected rows.
Project padding is included in scroll extents. Section headings use smaller muted
uppercase text with a separate right-aligned count; project open indicators appear
only in their overview control. This uses the existing configured font, with no
new font dependency. Native screenshots at wide and narrow widths provide the
visual check for these geometry changes.

### Workspace selection and hierarchy direction

Current-workspace indication uses a restrained theme-derived row fill and a
stronger fill on the exact inline action (or overflow control containing it).
The selection tint is blended toward normal text to reduce saturation, then into
the sidebar background. Persistent selection no longer adds blue outlines around
both rows and actions. The toolkit's keyboard-focus treatment remains independent.
Collapsed project/section ancestors retain the containing-row indication.

A convoy has its own state and may eventually offer an overview workspace for
more involved, graph-shaped workflows. Its status slot remains distinct from
vessel status. Inline vessels are the compact presentation; a future expansion
mode may put them on individual rows. Opening the convoy overview and expanding
its vessel presentation must be independent actions. Do not substitute a vessel
binding for the convoy merely to simplify the native row.

Collapsed projects may eventually show summaries of working/completed convoys
when space permits. Andamento should provide the summary semantics and expansion
state; Wheelhouse measures the available space. Such summaries must preserve
primary controls and must not be inferred from currently visible or materialized
workspaces. This change records that direction; it does not fabricate overview
recipes, introduce a new expansion mode, or add producer summary facts.
