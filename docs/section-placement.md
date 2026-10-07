# Section placement

Andamento declares region identity and content. `default-host="sidebar"` and
`order=10` supply initial placement hints; Wheelhouse saves the actual
positions in its existing docking trees. A region's `section` setting is its
stable id. The owning Controlled Split stores a `section_positions` inventory
alongside `control_views` and `floating_panels`. An inventory entry with
`closed` explicitly records an intentional close; missing entries represent
newly declared regions.

Reconciliation retains valid saved Views, removes disappeared regions and
their inventory entries, and places new ids at their hints. The shared docking
checker validates restored and new positions. Wheelhouse recognises `sidebar`
and `floating` host hints; absent, unknown or rejected hosts fall back to the
sidebar. Explicit orders sort lower first; an omitted order uses the region's
zero-based declaration index. Ties preserve declaration order. An unhinted region at index 2 sorts before
order=10, but after order=1; hint all regions or none to avoid mixed scales.
The exception is Andamento's synthetic Other workspaces section
(`andamento.unplaced-workspaces`). It is always emitted and unhinted, so
Wheelhouse places it after every declared region unless Andamento gives it an
order. New sections
are inserted before their next hinted neighbour in the same host, keeping the
saved sections' relative order and identities. Existing nested panel trees
remain intact. Unrelated empty saved panels are retained. KDL title changes
refresh existing View labels without moving their saved positions. Placement
hints are copied and sorted once per immutable snapshot; saved layout changes
still reconcile against that cached declaration.

A legacy saved sidebar host without an inventory is adopted once in place.
Even an empty saved sidebar root counts as legacy; a floating-only layout
without that root is treated as a fresh declaration inventory. Its
currently declared missing Views are recorded as intentional closes because
older Wheelhouse used the presence of the saved host to prevent reopening
them. Regions first declared on later updates are new inventory ids and appear
normally. Reset To Default Panel Layout (and the compact/simple variants)
clears section positions and closes at the root Controlled Split, restoring
current hints. Child Workspace layouts keep their own ownership. The persistent sidebar
footer's Sections menu lists declarations without a View at this level. Restore
clears that declaration's closed record and uses the shared placement checker
and current host hint. An existing docked or floating View is never duplicated.
The restored panel receives a sibling share, preserving the other panels'
relative allocations and lifting a merged leaf's existing tabs when needed.
The menu remains available when every fleet section, including Other workspaces,
is closed. A missing snapshot disables restoration without changing the saved
arrangement.

Run `python tools/test-section-placement.py` after building for the headless
native lifecycle scenarios and KDL source-integrity check. These exercise the
real Andamento ABI, shared docking checker and config serializer, including
restart, add/remove, reset, stable/tied orders, unknown hosts, provider loss,
nested saved splits and invalid saved placement. The source integrity runner
loads the shipped KDL before reordering/resetting its regions. An
authoritative empty declaration clears all positions and closed records; a
missing provider snapshot preserves both. Native snapshots are immutable,
complete acquisitions, so absence of section nodes is a declaration update,
not a partial reload. The operator performs the physical drag check after
merge.
